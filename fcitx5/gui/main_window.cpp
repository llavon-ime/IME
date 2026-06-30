#include "main_window.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QTabWidget>
#include <QVBoxLayout>
#include <QWidget>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <stdexcept>
#include <utility>

namespace ime::fcitx5::gui {

namespace {

QString text_or_dash(const std::string& value) {
    return value.empty() ? QStringLiteral("-") : QString::fromStdString(value);
}

QSpinBox* make_spin_box(int minimum, int maximum) {
    auto* spin = new QSpinBox;
    spin->setRange(minimum, maximum);
    return spin;
}

QComboBox* make_combo_box(std::initializer_list<std::pair<const char*, const char*>> items) {
    auto* combo = new QComboBox;
    for (const auto& [label, value] : items) combo->addItem(QString::fromUtf8(label), QString::fromUtf8(value));
    return combo;
}

void set_combo_value(QComboBox* combo, const std::string& value) {
    const int index = combo->findData(QString::fromStdString(value));
    combo->setCurrentIndex(index >= 0 ? index : 0);
}

std::string combo_value(const QComboBox* combo) {
    return combo->currentData().toString().toStdString();
}

}  // namespace

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent), config_(default_config()) {
    config_ = read_config();
    build_ui();
    populate_form();
    if (!load_error_.empty()) set_message(QString::fromStdString(load_error_));
}

void MainWindow::build_ui() {
    setWindowTitle(QStringLiteral("拉風輸入法設定"));
    resize(640, 420);

    auto* tabs = new QTabWidget(this);
    tabs->addTab(create_status_tab(), QStringLiteral("狀態"));
    tabs->addTab(create_model_tab(), QStringLiteral("模型"));
    tabs->addTab(create_service_tab(), QStringLiteral("服務"));
    tabs->addTab(create_input_tab(), QStringLiteral("輸入"));
    tabs->addTab(create_about_tab(), QStringLiteral("關於"));

    setCentralWidget(tabs);
}

QWidget* MainWindow::create_status_tab() {
    auto* tab = new QWidget;
    auto* layout = new QVBoxLayout(tab);
    auto* form = new QFormLayout;

    status_running_label_ = new QLabel(QStringLiteral("尚未更新"));
    status_model_loaded_label_ = new QLabel(QStringLiteral("尚未更新"));
    status_backend_label_ = new QLabel(QStringLiteral("尚未更新"));
    status_model_path_label_ = new QLabel(QStringLiteral("尚未更新"));
    status_error_label_ = new QLabel(QStringLiteral("尚未更新"));
    status_model_path_label_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    status_error_label_->setTextInteractionFlags(Qt::TextSelectableByMouse);

    form->addRow(QStringLiteral("執行中"), status_running_label_);
    form->addRow(QStringLiteral("模型已載入"), status_model_loaded_label_);
    form->addRow(QStringLiteral("後端"), status_backend_label_);
    form->addRow(QStringLiteral("模型路徑"), status_model_path_label_);
    form->addRow(QStringLiteral("錯誤"), status_error_label_);
    layout->addLayout(form);

    auto* button_row = new QHBoxLayout;
    auto* refresh_button = new QPushButton(QStringLiteral("重新整理"));
    auto* stop_button = new QPushButton(QStringLiteral("停止"));
    auto* restart_button = new QPushButton(QStringLiteral("重新啟動"));
    connect(refresh_button, &QPushButton::clicked, this, &MainWindow::refresh_status);
    connect(stop_button, &QPushButton::clicked, this, &MainWindow::stop_service);
    connect(restart_button, &QPushButton::clicked, this, &MainWindow::restart_service);
    button_row->addWidget(refresh_button);
    button_row->addWidget(stop_button);
    button_row->addWidget(restart_button);
    button_row->addStretch();
    layout->addLayout(button_row);

    message_label_ = new QLabel;
    message_label_->setWordWrap(true);
    message_label_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(message_label_);
    layout->addStretch();
    return tab;
}

QWidget* MainWindow::create_model_tab() {
    auto* tab = new QWidget;
    auto* layout = new QVBoxLayout(tab);
    auto* form = new QFormLayout;

    auto* model_row = new QWidget;
    auto* model_layout = new QHBoxLayout(model_row);
    model_layout->setContentsMargins(0, 0, 0, 0);
    model_path_edit_ = new QLineEdit;
    auto* browse_button = new QPushButton(QStringLiteral("瀏覽..."));
    connect(browse_button, &QPushButton::clicked, this, &MainWindow::choose_model_path);
    model_layout->addWidget(model_path_edit_);
    model_layout->addWidget(browse_button);
    form->addRow(QStringLiteral("模型路徑"), model_row);

    context_length_spin_ = make_spin_box(1, 1048576);
    thread_count_spin_ = make_spin_box(1, 1024);
    gpu_layers_spin_ = make_spin_box(0, 1024);
    form->addRow(QStringLiteral("上下文長度"), context_length_spin_);
    form->addRow(QStringLiteral("執行緒數"), thread_count_spin_);
    form->addRow(QStringLiteral("顯示卡分層數"), gpu_layers_spin_);
    layout->addLayout(form);

    auto* save_button = new QPushButton(QStringLiteral("儲存"));
    connect(save_button, &QPushButton::clicked, this, &MainWindow::save_config);
    layout->addWidget(save_button);
    layout->addStretch();
    return tab;
}

QWidget* MainWindow::create_service_tab() {
    auto* tab = new QWidget;
    auto* layout = new QVBoxLayout(tab);
    auto* form = new QFormLayout;

    idle_timeout_spin_ = make_spin_box(0, 86400);
    idle_timeout_spin_->setSuffix(QStringLiteral(" 秒"));
    form->addRow(QStringLiteral("閒置逾時"), idle_timeout_spin_);
    layout->addLayout(form);

    auto* save_button = new QPushButton(QStringLiteral("儲存"));
    connect(save_button, &QPushButton::clicked, this, &MainWindow::save_config);
    layout->addWidget(save_button);
    layout->addStretch();
    return tab;
}

QWidget* MainWindow::create_input_tab() {
    auto* tab = new QWidget;
    auto* layout = new QVBoxLayout(tab);
    auto* form = new QFormLayout;

    keyboard_layout_combo_ = make_combo_box({{"標準", "standard"}});
    selection_keys_combo_ =
        make_combo_box({{"數字鍵", "123456789"}, {"本位列", "asdfghjkl"}, {"左手鍵", "asdfzxcvb"}});
    selection_key_count_spin_ = make_spin_box(4, 9);
    candidate_page_size_spin_ = make_spin_box(1, 50);
    candidate_layout_combo_ =
        make_combo_box({{"系統預設", "not_set"}, {"垂直", "vertical"}, {"水平", "horizontal"}});
    space_selects_candidate_check_ =
        new QCheckBox(QStringLiteral("候選窗開啟時，空白鍵選取高亮候選字"));
    select_phrase_combo_ = make_combo_box({{"游標前", "before_cursor"}, {"游標後", "after_cursor"}});
    move_cursor_after_selection_check_ = new QCheckBox(QStringLiteral("選字後移動游標"));
    esc_clears_entire_buffer_check_ = new QCheckBox(QStringLiteral("逸出鍵直接清空整個組字區"));
    caps_lock_inputs_bopomofo_check_ = new QCheckBox(QStringLiteral("大寫鎖定時仍輸入注音"));

    form->addRow(QStringLiteral("注音鍵盤配置"), keyboard_layout_combo_);
    form->addRow(QStringLiteral("候選選字鍵"), selection_keys_combo_);
    form->addRow(QStringLiteral("候選選字鍵數量"), selection_key_count_spin_);
    form->addRow(QStringLiteral("候選頁大小"), candidate_page_size_spin_);
    form->addRow(QStringLiteral("候選窗排列"), candidate_layout_combo_);
    form->addRow(QStringLiteral("空白鍵行為"), space_selects_candidate_check_);
    form->addRow(QStringLiteral("候選字查詢位置"), select_phrase_combo_);
    form->addRow(QStringLiteral("選字後行為"), move_cursor_after_selection_check_);
    form->addRow(QStringLiteral("逸出鍵行為"), esc_clears_entire_buffer_check_);
    form->addRow(QStringLiteral("大寫鎖定行為"), caps_lock_inputs_bopomofo_check_);
    layout->addLayout(form);

    auto* save_button = new QPushButton(QStringLiteral("儲存"));
    connect(save_button, &QPushButton::clicked, this, &MainWindow::save_config);
    layout->addWidget(save_button);
    layout->addStretch();
    return tab;
}

QWidget* MainWindow::create_about_tab() {
    auto* tab = new QWidget;
    auto* layout = new QVBoxLayout(tab);
    auto* title = new QLabel(QStringLiteral("拉風輸入法設定"));
    auto* body = new QLabel(QStringLiteral("拉風輸入法的 Qt6 原生設定工具。"));
    body->setWordWrap(true);
    layout->addWidget(title);
    layout->addWidget(body);
    layout->addStretch();
    return tab;
}

Config MainWindow::read_config() {
    const auto path = config_path();
    if (!std::filesystem::exists(path)) return default_config();

    try {
        std::ifstream input(path);
        if (!input) throw std::runtime_error("failed to open config file");
        nlohmann::json json;
        input >> json;
        return config_from_json(json);
    } catch (const std::exception& error) {
        load_error_ = "Failed to load " + path.string() + ": " + error.what();
        return default_config();
    }
}

void MainWindow::populate_form() {
    model_path_edit_->setText(QString::fromStdString(config_.model_path));
    context_length_spin_->setValue(config_.context_length);
    thread_count_spin_->setValue(config_.thread_count);
    gpu_layers_spin_->setValue(config_.gpu_layers);
    idle_timeout_spin_->setValue(config_.idle_timeout_seconds);
    set_combo_value(keyboard_layout_combo_, config_.keyboard_layout);
    set_combo_value(selection_keys_combo_, config_.selection_keys);
    selection_key_count_spin_->setValue(config_.selection_key_count);
    candidate_page_size_spin_->setValue(config_.candidate_page_size);
    set_combo_value(candidate_layout_combo_, config_.candidate_layout);
    space_selects_candidate_check_->setChecked(config_.space_selects_candidate);
    set_combo_value(select_phrase_combo_, config_.select_phrase);
    move_cursor_after_selection_check_->setChecked(config_.move_cursor_after_selection);
    esc_clears_entire_buffer_check_->setChecked(config_.esc_clears_entire_buffer);
    caps_lock_inputs_bopomofo_check_->setChecked(config_.caps_lock_inputs_bopomofo);
}

void MainWindow::collect_form() {
    config_.model_path = model_path_edit_->text().toStdString();
    config_.context_length = context_length_spin_->value();
    config_.thread_count = thread_count_spin_->value();
    config_.gpu_layers = gpu_layers_spin_->value();
    config_.idle_timeout_seconds = idle_timeout_spin_->value();
    config_.keyboard_layout = combo_value(keyboard_layout_combo_);
    config_.selection_keys = combo_value(selection_keys_combo_);
    config_.selection_key_count = selection_key_count_spin_->value();
    config_.candidate_page_size = candidate_page_size_spin_->value();
    config_.candidate_layout = combo_value(candidate_layout_combo_);
    config_.space_selects_candidate = space_selects_candidate_check_->isChecked();
    config_.select_phrase = combo_value(select_phrase_combo_);
    config_.move_cursor_after_selection = move_cursor_after_selection_check_->isChecked();
    config_.esc_clears_entire_buffer = esc_clears_entire_buffer_check_->isChecked();
    config_.caps_lock_inputs_bopomofo = caps_lock_inputs_bopomofo_check_->isChecked();
}

void MainWindow::choose_model_path() {
    const QString selected = QFileDialog::getOpenFileName(
        this, QStringLiteral("選擇 GGUF 模型"), model_path_edit_->text(),
        QStringLiteral("GGUF 模型 (*.gguf);;所有檔案 (*)"));
    if (!selected.isEmpty()) model_path_edit_->setText(selected);
}

void MainWindow::save_config() {
    collect_form();
    const auto path = config_path();

    try {
        if (!path.parent_path().empty()) std::filesystem::create_directories(path.parent_path());
        std::ofstream output(path);
        if (!output) throw std::runtime_error("failed to open config file for writing");
        output << to_json(config_).dump(2) << '\n';
        set_message(QStringLiteral("已儲存 %1").arg(QString::fromStdString(path.string())));
    } catch (const std::exception& error) {
        set_message(QStringLiteral("儲存 %1 失敗: %2")
                        .arg(QString::fromStdString(path.string()), QString::fromStdString(error.what())));
    }
}

void MainWindow::refresh_status() {
    show_status(service_client_.status());
}

void MainWindow::stop_service() {
    show_status(service_client_.stop());
}

void MainWindow::restart_service() {
    (void)service_client_.stop();
    if (!service_client_.start_service_if_needed()) {
        StatusResponse status;
        status.running = false;
        status.backend = "unavailable";
        status.error = "unable to start service";
        show_status(status);
        return;
    }
    show_status(service_client_.status());
}

void MainWindow::show_status(const StatusResponse& status) {
    status_running_label_->setText(status.running ? QStringLiteral("是") : QStringLiteral("否"));
    status_model_loaded_label_->setText(status.model_loaded ? QStringLiteral("是") : QStringLiteral("否"));
    status_backend_label_->setText(text_or_dash(status.backend));
    status_model_path_label_->setText(text_or_dash(status.model_path));
    status_error_label_->setText(text_or_dash(status.error));
}

void MainWindow::set_message(const QString& message) {
    if (message_label_ != nullptr) message_label_->setText(message);
}

}  // namespace ime::fcitx5::gui
