#pragma once

#include <QMainWindow>
#include <string>

#include "config/config.hpp"
#include "engine/service_client.hpp"
#include "protocol/protocol.hpp"

class QLabel;
class QCheckBox;
class QComboBox;
class QLineEdit;
class QPushButton;
class QSpinBox;
class QWidget;

namespace ime::linux::gui {

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);

private:
    void build_ui();
    QWidget* create_status_tab();
    QWidget* create_model_tab();
    QWidget* create_service_tab();
    QWidget* create_input_tab();
    QWidget* create_about_tab();
    Config read_config();
    void populate_form();
    void collect_form();
    void choose_model_path();
    void save_config();
    void refresh_status();
    void stop_service();
    void restart_service();
    void show_status(const StatusResponse& status);
    void set_message(const QString& message);

    Config config_;
    ServiceClient service_client_;
    std::string load_error_;

    QLabel* status_running_label_ = nullptr;
    QLabel* status_model_loaded_label_ = nullptr;
    QLabel* status_backend_label_ = nullptr;
    QLabel* status_model_path_label_ = nullptr;
    QLabel* status_error_label_ = nullptr;
    QLabel* message_label_ = nullptr;
    QLineEdit* model_path_edit_ = nullptr;
    QSpinBox* context_length_spin_ = nullptr;
    QSpinBox* thread_count_spin_ = nullptr;
    QSpinBox* gpu_layers_spin_ = nullptr;
    QSpinBox* idle_timeout_spin_ = nullptr;
    QComboBox* keyboard_layout_combo_ = nullptr;
    QComboBox* selection_keys_combo_ = nullptr;
    QSpinBox* selection_key_count_spin_ = nullptr;
    QSpinBox* candidate_page_size_spin_ = nullptr;
    QComboBox* candidate_layout_combo_ = nullptr;
    QCheckBox* space_selects_candidate_check_ = nullptr;
    QComboBox* select_phrase_combo_ = nullptr;
    QCheckBox* move_cursor_after_selection_check_ = nullptr;
    QCheckBox* esc_clears_entire_buffer_check_ = nullptr;
    QCheckBox* caps_lock_inputs_bopomofo_check_ = nullptr;
};

}  // namespace ime::linux::gui
