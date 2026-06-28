#include "bopomofo/keymap.hpp"
#include "bopomofo/syllable.hpp"
#include "bopomofo/table_engine.hpp"

#include <cstdlib>
#include <string>

int run_bopomofo_tests() {
    bool ok = true;
    ok = ok && ime::linux::lookup_bopomofo_key(U'1') == U'ㄅ';
    ok = ok && ime::linux::lookup_bopomofo_key(U'4') == U'ˋ';
    ok = ok && ime::linux::lookup_bopomofo_key(U'q') == U'ㄆ';
    ok = ok && ime::linux::lookup_bopomofo_key(U'Q') == U'ㄆ';
    ok = ok && ime::linux::lookup_bopomofo_key(U'S') == U'ㄋ';
    ok = ok && !ime::linux::lookup_bopomofo_key(U'Q', false).has_value();
    ok = ok && ime::linux::lookup_bopomofo_key(U' ') == U' ';
    ok = ok && !ime::linux::lookup_bopomofo_key(U'@').has_value();
    ok = ok && ime::linux::lookup_microsoft_punctuation_key(U'!') == U'！';
    ok = ok && ime::linux::lookup_microsoft_punctuation_key(U'>') == U'。';
    ok = ok && ime::linux::lookup_microsoft_punctuation_key(U'<') == U'，';
    ok = ok && ime::linux::lookup_microsoft_punctuation_key(U'\'') == U'、';
    ok = ok && ime::linux::lookup_microsoft_ctrl_punctuation_key(U'!') == U'！';
    ok = ok && ime::linux::lookup_microsoft_ctrl_punctuation_key(U'.') == U'。';

    ime::linux::Syllable syllable;
    ok = ok && syllable.accept(U'ㄋ');
    ok = ok && syllable.accept(U'ㄧ');
    ok = ok && syllable.accept(U'ˇ');
    ok = ok && syllable.complete();
    ok = ok && syllable.text() == std::u16string(u"ㄋㄧˇ");

    ime::linux::Syllable tone_only;
    ok = ok && tone_only.accept(U'ˋ');
    ok = ok && !tone_only.complete();
    ok = ok && tone_only.text() == std::u16string(u"ˋ");

    ime::linux::TableEngine table("tables/bopomofo_char.json");
    auto candidates = table.lookup(u"ㄋㄧˇ");
    ok = ok && !candidates.empty();
    return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
