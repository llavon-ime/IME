from __future__ import annotations

import argparse
import json
import shutil
import subprocess
import sys
from pathlib import Path

import torch
from safetensors import safe_open

from gguf import GGUFWriter, RopeScalingType


DEFAULT_CKPT = Path("models/step-100000")
DEFAULT_OUTFILE = Path("models/step-100000/model-fixed-f16.gguf")
DEFAULT_LLAMA_BIN = Path("llama-b9174-bin-win-cpu-x64")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description=(
            "Convert the custom IME Hugging Face Llama checkpoint to GGUF. "
            "This script explicitly applies the Q/K permutation expected by llama.cpp."
        )
    )
    parser.add_argument("--checkpoint", type=Path, default=DEFAULT_CKPT)
    parser.add_argument("--outfile", type=Path, default=DEFAULT_OUTFILE)
    parser.add_argument("--outtype", choices=("f16", "f32"), default="f16")
    parser.add_argument("--force", action="store_true")
    parser.add_argument("--quantize", default=None, help="Optional llama-quantize type, e.g. Q4_K_M or Q5_K_M.")
    parser.add_argument("--quantized-outfile", type=Path, default=None)
    parser.add_argument("--llama-bin-dir", type=Path, default=DEFAULT_LLAMA_BIN)
    return parser.parse_args()


def require_file(path: Path) -> Path:
    if not path.is_file():
        raise FileNotFoundError(path)
    return path


def load_json(path: Path) -> dict:
    return json.loads(path.read_text("utf-8"))


def load_safetensors(path: Path) -> dict[str, torch.Tensor]:
    tensors: dict[str, torch.Tensor] = {}
    with safe_open(str(path), framework="pt", device="cpu") as f:
        for key in f.keys():
            tensors[key] = f.get_tensor(key)
    return tensors


def hf_to_gguf_name(name: str) -> str:
    replacements = [
        ("model.embed_tokens.weight", "token_embd.weight"),
        ("model.norm.weight", "output_norm.weight"),
        ("lm_head.weight", "output.weight"),
        ("model.layers.", "blk."),
        (".self_attn.q_proj.weight", ".attn_q.weight"),
        (".self_attn.k_proj.weight", ".attn_k.weight"),
        (".self_attn.v_proj.weight", ".attn_v.weight"),
        (".self_attn.o_proj.weight", ".attn_output.weight"),
        (".mlp.gate_proj.weight", ".ffn_gate.weight"),
        (".mlp.up_proj.weight", ".ffn_up.weight"),
        (".mlp.down_proj.weight", ".ffn_down.weight"),
        (".input_layernorm.weight", ".attn_norm.weight"),
        (".post_attention_layernorm.weight", ".ffn_norm.weight"),
    ]
    out = name
    for old, new in replacements:
        out = out.replace(old, new)
    return out


def permute_qk_for_llamacpp(weight: torch.Tensor, n_heads: int) -> torch.Tensor:
    """Convert HF Llama Q/K projection layout into llama.cpp GGUF layout."""
    if weight.ndim != 2:
        raise ValueError(f"expected 2D Q/K weight, got shape={tuple(weight.shape)}")
    rows = weight.shape[0]
    if rows % n_heads != 0:
        raise ValueError(f"rows={rows} is not divisible by n_heads={n_heads}")
    head_dim = rows // n_heads
    if head_dim % 2 != 0:
        raise ValueError(f"head_dim={head_dim} must be even for RoPE permutation")
    return (
        weight.reshape(n_heads, 2, head_dim // 2, *weight.shape[1:])
        .swapaxes(1, 2)
        .reshape_as(weight)
        .contiguous()
    )


def reverse_permute_qk_from_llamacpp(weight: torch.Tensor, n_heads: int) -> torch.Tensor:
    """Inverse of permute_qk_for_llamacpp, useful for sanity checks."""
    if weight.ndim != 2:
        raise ValueError(f"expected 2D Q/K weight, got shape={tuple(weight.shape)}")
    rows = weight.shape[0]
    head_dim = rows // n_heads
    return (
        weight.reshape(n_heads, head_dim // 2, 2, *weight.shape[1:])
        .swapaxes(1, 2)
        .reshape_as(weight)
        .contiguous()
    )


def add_metadata(writer: GGUFWriter, cfg: dict, tokens: list[str], outtype: str) -> None:
    n_heads = int(cfg["num_attention_heads"])
    n_kv_heads = int(cfg.get("num_key_value_heads", n_heads))
    hidden_size = int(cfg["hidden_size"])
    head_dim = int(cfg.get("head_dim", hidden_size // n_heads))
    rope_theta = cfg.get("rope_theta", cfg.get("rope_parameters", {}).get("rope_theta", 10000.0))

    writer.add_file_type(1 if outtype == "f16" else 0)
    writer.add_context_length(int(cfg.get("max_position_embeddings", 384)))
    writer.add_embedding_length(hidden_size)
    writer.add_feed_forward_length(int(cfg["intermediate_size"]))
    writer.add_block_count(int(cfg["num_hidden_layers"]))
    writer.add_head_count(n_heads)
    writer.add_head_count_kv(n_kv_heads)
    writer.add_layer_norm_rms_eps(float(cfg.get("rms_norm_eps", 1e-5)))
    writer.add_rope_dimension_count(head_dim)
    writer.add_rope_freq_base(float(rope_theta))
    writer.add_rope_scaling_type(RopeScalingType.NONE)

    # The IME does external tokenization from ime_vocab.json. llama.cpp still
    # requires tokenizer metadata for vocab size, special ids, and validation.
    writer.add_tokenizer_model("gpt2")
    writer.add_tokenizer_pre("default")
    writer.add_token_list(tokens)
    writer.add_token_scores([0.0] * len(tokens))

    token_types = [1] * len(tokens)
    for i, token in enumerate(tokens):
        if token in {"<PAD>", "<BOS>", "<EOS>", "<UNK>"}:
            token_types[i] = 3
        elif token.startswith("<") and token.endswith(">"):
            token_types[i] = 4
    writer.add_token_types(token_types)
    writer.add_token_merges(["<PAD> <BOS>"])

    writer.add_bos_token_id(int(cfg.get("bos_token_id", 1)))
    writer.add_eos_token_id(int(cfg.get("eos_token_id", 2)))
    writer.add_unk_token_id(4)
    writer.add_pad_token_id(int(cfg.get("pad_token_id", 0)))
    writer.add_add_bos_token(True)


def add_tensor(writer: GGUFWriter, name: str, tensor: torch.Tensor, outtype: str, keep_f32: bool = False) -> None:
    data = tensor.detach().cpu().contiguous()
    if outtype == "f16" and not keep_f32:
        data = data.to(torch.float16)
    else:
        data = data.to(torch.float32)
    writer.add_tensor(name, data.numpy())


def convert(checkpoint: Path, outfile: Path, outtype: str, force: bool) -> None:
    checkpoint = checkpoint.resolve()
    outfile = outfile.resolve()

    require_file(checkpoint / "config.json")
    require_file(checkpoint / "ime_vocab.json")
    require_file(checkpoint / "model.safetensors")

    if outfile.exists() and not force:
        raise FileExistsError(f"{outfile} exists; pass --force to overwrite")
    outfile.parent.mkdir(parents=True, exist_ok=True)

    cfg = load_json(checkpoint / "config.json")
    vocab = load_json(checkpoint / "ime_vocab.json")
    tokens = vocab["tokens"]
    weights = load_safetensors(checkpoint / "model.safetensors")

    n_layers = int(cfg["num_hidden_layers"])
    n_heads = int(cfg["num_attention_heads"])
    n_kv_heads = int(cfg.get("num_key_value_heads", n_heads))

    if len(tokens) != int(cfg["vocab_size"]):
        raise ValueError(f"vocab size mismatch: tokens={len(tokens)} config={cfg['vocab_size']}")
    if tokens[1406] != "是" or tokens[1734] != "視":
        raise ValueError(f"unexpected key token ids: 1406={tokens[1406]!r}, 1734={tokens[1734]!r}")

    writer = GGUFWriter(str(outfile), "llama")
    add_metadata(writer, cfg, tokens, outtype)

    def need(key: str) -> torch.Tensor:
        if key not in weights:
            raise KeyError(key)
        return weights[key]

    q0 = need("model.layers.0.self_attn.q_proj.weight")
    q0_perm = permute_qk_for_llamacpp(q0, n_heads)
    q0_roundtrip = reverse_permute_qk_from_llamacpp(q0_perm, n_heads)
    roundtrip_diff = (q0.float() - q0_roundtrip.float()).abs().max().item()
    direct_diff = (q0.float() - q0_perm.float()).abs().max().item()
    print(f"Q/K permutation sanity: q0 direct_diff={direct_diff:.6f}, roundtrip_diff={roundtrip_diff:.8f}")
    if direct_diff == 0:
        raise RuntimeError("Q permutation did not change layer 0; this should not happen")
    if roundtrip_diff > 0:
        raise RuntimeError(f"Q permutation roundtrip failed: max_diff={roundtrip_diff}")

    add_tensor(writer, "token_embd.weight", need("model.embed_tokens.weight"), outtype)
    for layer in range(n_layers):
        prefix = f"model.layers.{layer}"
        gguf_prefix = f"blk.{layer}"

        add_tensor(writer, f"{gguf_prefix}.attn_norm.weight", need(f"{prefix}.input_layernorm.weight"), outtype, True)
        add_tensor(
            writer,
            f"{gguf_prefix}.attn_q.weight",
            permute_qk_for_llamacpp(need(f"{prefix}.self_attn.q_proj.weight"), n_heads),
            outtype,
        )
        add_tensor(
            writer,
            f"{gguf_prefix}.attn_k.weight",
            permute_qk_for_llamacpp(need(f"{prefix}.self_attn.k_proj.weight"), n_kv_heads),
            outtype,
        )
        add_tensor(writer, f"{gguf_prefix}.attn_v.weight", need(f"{prefix}.self_attn.v_proj.weight"), outtype)
        add_tensor(writer, f"{gguf_prefix}.attn_output.weight", need(f"{prefix}.self_attn.o_proj.weight"), outtype)

        add_tensor(
            writer,
            f"{gguf_prefix}.ffn_norm.weight",
            need(f"{prefix}.post_attention_layernorm.weight"),
            outtype,
            True,
        )
        add_tensor(writer, f"{gguf_prefix}.ffn_gate.weight", need(f"{prefix}.mlp.gate_proj.weight"), outtype)
        add_tensor(writer, f"{gguf_prefix}.ffn_up.weight", need(f"{prefix}.mlp.up_proj.weight"), outtype)
        add_tensor(writer, f"{gguf_prefix}.ffn_down.weight", need(f"{prefix}.mlp.down_proj.weight"), outtype)

    add_tensor(writer, "output_norm.weight", need("model.norm.weight"), outtype, True)
    add_tensor(writer, "output.weight", need("lm_head.weight"), outtype)

    writer.write_header_to_file()
    writer.write_kv_data_to_file()
    writer.write_tensors_to_file()
    writer.close()

    print(f"Wrote {outfile} ({outfile.stat().st_size / 1024 / 1024:.1f} MiB)")


def find_quantize(llama_bin_dir: Path) -> Path:
    candidates = [
        llama_bin_dir / "llama-quantize.exe",
        llama_bin_dir / "llama-quantize",
        Path(shutil.which("llama-quantize") or ""),
        Path(shutil.which("llama-quantize.exe") or ""),
    ]
    for candidate in candidates:
        if candidate and candidate.is_file():
            return candidate.resolve()
    raise FileNotFoundError("llama-quantize not found; pass --llama-bin-dir")


def quantize(f16_file: Path, quantized_outfile: Path, quant_type: str, llama_bin_dir: Path, force: bool) -> None:
    if quantized_outfile.exists() and not force:
        raise FileExistsError(f"{quantized_outfile} exists; pass --force to overwrite")
    quantized_outfile.parent.mkdir(parents=True, exist_ok=True)
    binary = find_quantize(llama_bin_dir)
    cmd = [str(binary), str(f16_file.resolve()), str(quantized_outfile.resolve()), quant_type]
    print("$ " + subprocess.list2cmdline(cmd))
    subprocess.run(cmd, check=True)
    print(f"Wrote {quantized_outfile} ({quantized_outfile.stat().st_size / 1024 / 1024:.1f} MiB)")


def main() -> int:
    args = parse_args()
    convert(args.checkpoint, args.outfile, args.outtype, args.force)

    if args.quantize:
        q_out = args.quantized_outfile
        if q_out is None:
            q_out = args.outfile.with_name(f"{args.outfile.stem}-{args.quantize}.gguf")
        quantize(args.outfile, q_out, args.quantize, args.llama_bin_dir, args.force)

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
