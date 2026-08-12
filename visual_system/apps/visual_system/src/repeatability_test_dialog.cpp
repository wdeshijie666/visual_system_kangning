/**
 * @file repeatability_test_dialog.cpp
 * @brief 重复精度测试窗口：双模式编排周期、仅统计 OK Log、刷新彩色结果表。
 */
#include "repeatability_test_dialog.h"

#include <QComboBox>
#include <QDir>
#include <QDirIterator>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QMetaObject>
#include <QPointer>
#include <QPushButton>
#include <QSpinBox>
#include <QStackedWidget>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>

#include "visual/app_context.h"
#include "visual/data_recorder.h"
#include "visual/event_bus.h"

namespace {

constexpr int kFieldCount = 5;  // X Y R Diameter Length，不含 Status

QColor ColorDelta() {
  return QColor(180, 140, 0);
}
QColor ColorMax() {
  return QColor(200, 40, 40);
}
QColor ColorMin() {
  return QColor(20, 140, 60);
}

void UpdateField(RepeatabilityFieldExtrema* f, double value, const QString& path) {
  if (f == nullptr) {
    return;
  }
  if (!f->has_max || value > f->max_v) {
    f->has_max = true;
    f->max_v = value;
    f->max_path = path;
  }
  if (!f->has_min || value < f->min_v) {
    f->has_min = true;
    f->min_v = value;
    f->min_path = path;
  }
}

void AbsorbOkLogs(std::array<RepeatabilityLogExtrema, visual::kLogCountPerStation>* dest,
                  const visual::LogResultBatch& logs, const QString& depth_path) {
  if (dest == nullptr) {
    return;
  }
  for (std::size_t i = 0; i < logs.size(); ++i) {
    const auto& L = logs[i];
    if (L.status != visual::InspectStatus::kOk) {
      continue;
    }
    auto& e = (*dest)[i];
    UpdateField(&e.x, L.offset_x_mm, depth_path);
    UpdateField(&e.y, L.offset_y_mm, depth_path);
    UpdateField(&e.r, L.offset_r_deg, depth_path);
    UpdateField(&e.diameter, L.diameter_mm, depth_path);
    UpdateField(&e.length, L.length_mm, depth_path);
  }
}

QString ResolveDepthPath(visual::StationId station, const visual::CycleResultEvent& ev,
                         const QString& forced_depth) {
  if (!forced_depth.isEmpty()) {
    return forced_depth;
  }
  const std::string tag = (station == visual::StationId::kR09) ? "R09" : "R05";
  const std::string found =
      visual::FindDepthImageInSession(ev.session_dir.toStdString(), tag, "");
  if (!found.empty()) {
    return QString::fromStdString(found);
  }
  return ev.session_dir;
}

QTableWidgetItem* MakeTextItem(const QString& text, const QColor& color) {
  auto* item = new QTableWidgetItem(text);
  item->setForeground(color);
  item->setFlags(item->flags() & ~Qt::ItemIsEditable);
  return item;
}

QString FmtNum(bool has, double v) {
  if (!has) {
    return QStringLiteral("—");
  }
  return QString::number(v, 'f', 3);
}

const RepeatabilityFieldExtrema* FieldAt(const RepeatabilityLogExtrema& e, int field_idx) {
  switch (field_idx) {
    case 0:
      return &e.x;
    case 1:
      return &e.y;
    case 2:
      return &e.r;
    case 3:
      return &e.diameter;
    case 4:
      return &e.length;
    default:
      return &e.x;
  }
}

}  // namespace

RepeatabilityTestDialog::RepeatabilityTestDialog(std::shared_ptr<visual::SequenceEngine> engine,
                                                 QWidget* parent)
    : QDialog(parent), engine_(std::move(engine)) {
  setWindowTitle(tr("重复精度测试"));
  resize(1100, 640);

  station_combo_ = new QComboBox(this);
  station_combo_->addItem(QStringLiteral("R05"), static_cast<int>(visual::StationId::kR05));
  station_combo_->addItem(QStringLiteral("R09"), static_cast<int>(visual::StationId::kR09));

  mode_combo_ = new QComboBox(this);
  mode_combo_->addItem(tr("实机连续触发"), static_cast<int>(Mode::kLive));
  mode_combo_->addItem(tr("离线深度文件夹"), static_cast<int>(Mode::kOfflineFolder));

  live_count_spin_ = new QSpinBox(this);
  live_count_spin_->setRange(1, 10000);
  live_count_spin_->setValue(10);

  folder_edit_ = new QLineEdit(this);
  browse_button_ = new QPushButton(tr("浏览…"), this);

  auto* live_page = new QWidget(this);
  auto* live_form = new QFormLayout(live_page);
  live_form->addRow(tr("次数 N"), live_count_spin_);

  auto* offline_page = new QWidget(this);
  auto* offline_row = new QHBoxLayout(offline_page);
  offline_row->addWidget(folder_edit_, 1);
  offline_row->addWidget(browse_button_);

  mode_stack_ = new QStackedWidget(this);
  mode_stack_->addWidget(live_page);
  mode_stack_->addWidget(offline_page);

  start_button_ = new QPushButton(tr("开始"), this);
  stop_button_ = new QPushButton(tr("停止"), this);
  stop_button_->setEnabled(false);
  progress_label_ = new QLabel(tr("就绪"), this);

  result_table_ = new QTableWidget(this);
  result_table_->setColumnCount(1 + kFieldCount * 2);
  QStringList headers;
  headers << tr("行");
  const QStringList fields = {tr("X(mm)"), tr("Y(mm)"), tr("R(°)"), tr("直径(mm)"), tr("长度(mm)")};
  for (const QString& f : fields) {
    headers << f << tr("路径");
  }
  result_table_->setHorizontalHeaderLabels(headers);
  result_table_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
  result_table_->horizontalHeader()->setStretchLastSection(true);

  auto* form = new QFormLayout();
  form->addRow(tr("工位"), station_combo_);
  form->addRow(tr("模式"), mode_combo_);
  form->addRow(tr("参数"), mode_stack_);

  auto* btn_row = new QHBoxLayout();
  btn_row->addWidget(start_button_);
  btn_row->addWidget(stop_button_);
  btn_row->addWidget(progress_label_, 1);

  auto* root = new QVBoxLayout(this);
  root->addLayout(form);
  root->addLayout(btn_row);
  root->addWidget(result_table_, 1);

  connect(mode_combo_, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
          &RepeatabilityTestDialog::OnModeChanged);
  connect(browse_button_, &QPushButton::clicked, this, &RepeatabilityTestDialog::OnBrowseFolder);
  connect(start_button_, &QPushButton::clicked, this, &RepeatabilityTestDialog::OnStart);
  connect(stop_button_, &QPushButton::clicked, this, &RepeatabilityTestDialog::OnStop);

  ApplyModeUi();
}

RepeatabilityTestDialog::~RepeatabilityTestDialog() {
  cancel_.store(true);
  if (worker_.joinable()) {
    worker_.join();
  }
}

void RepeatabilityTestDialog::OnModeChanged(int) {
  ApplyModeUi();
}

void RepeatabilityTestDialog::ApplyModeUi() {
  const int mode = mode_combo_->currentData().toInt();
  mode_stack_->setCurrentIndex(mode == static_cast<int>(Mode::kOfflineFolder) ? 1 : 0);
}

void RepeatabilityTestDialog::OnBrowseFolder() {
  const QString default_dir = QString::fromStdString(
      visual::ResolveDataRoot(visual::AppContext::Instance().Settings().data_path));
  const QString dir = QFileDialog::getExistingDirectory(this, tr("选择离线数据文件夹"), default_dir);
  if (!dir.isEmpty()) {
    folder_edit_->setText(dir);
  }
}

void RepeatabilityTestDialog::SetRunningUi(bool running) {
  start_button_->setEnabled(!running);
  stop_button_->setEnabled(running);
  station_combo_->setEnabled(!running);
  mode_combo_->setEnabled(!running);
  live_count_spin_->setEnabled(!running);
  folder_edit_->setEnabled(!running);
  browse_button_->setEnabled(!running);
}

void RepeatabilityTestDialog::OnStop() {
  cancel_.store(true);
  progress_label_->setText(tr("正在停止…"));
}

void RepeatabilityTestDialog::OnProgress(int done, int total, const QString& detail) {
  progress_label_->setText(tr("进度 %1/%2  %3").arg(done).arg(total).arg(detail));
}

void RepeatabilityTestDialog::OnFinished(bool ok, const QString& message) {
  SetRunningUi(false);
  running_.store(false);
  emit BusyChanged(false);
  FillResultTable();
  progress_label_->setText(message);
  if (!ok) {
    QMessageBox::warning(this, tr("重复精度测试"), message);
  }
}

QStringList RepeatabilityTestDialog::CollectDepthFiles(const QString& root_dir) const {
  QStringList out;
  if (root_dir.isEmpty()) {
    return out;
  }
  QDirIterator it(root_dir, QDir::Files, QDirIterator::Subdirectories);
  while (it.hasNext()) {
    it.next();
    const QString name = it.fileName();
    if (name.contains(QStringLiteral("_depth."), Qt::CaseInsensitive)) {
      out << it.filePath();
    }
  }
  out.sort(Qt::CaseInsensitive);
  return out;
}

void RepeatabilityTestDialog::OnStart() {
  if (!engine_ || running_.load()) {
    return;
  }
  if (engine_->IsRunning()) {
    QMessageBox::warning(this, tr("重复精度测试"), tr("请先停止产线，再开始精度测试"));
    return;
  }

  const Mode mode = static_cast<Mode>(mode_combo_->currentData().toInt());
  const auto station = static_cast<visual::StationId>(station_combo_->currentData().toInt());
  const int live_n = live_count_spin_->value();
  const QString folder = folder_edit_->text().trimmed();

  if (mode == Mode::kOfflineFolder && folder.isEmpty()) {
    QMessageBox::warning(this, tr("重复精度测试"), tr("请先选择离线数据文件夹"));
    return;
  }
  QStringList offline_files;
  if (mode == Mode::kOfflineFolder) {
    offline_files = CollectDepthFiles(folder);
    if (offline_files.isEmpty()) {
      QMessageBox::warning(this, tr("重复精度测试"),
                           tr("文件夹内未找到深度文件（文件名需含 _depth.）"));
      return;
    }
  }

  extrema_ = {};
  result_table_->setRowCount(0);
  cancel_.store(false);
  running_.store(true);
  SetRunningUi(true);
  emit BusyChanged(true);
  progress_label_->setText(tr("准备中…"));

  if (worker_.joinable()) {
    worker_.join();
  }
  worker_ = std::thread([this, mode, station, live_n, offline_files]() {
    RunWorker(mode, station, live_n, offline_files);
  });
}

void RepeatabilityTestDialog::RunWorker(Mode mode, visual::StationId station, int live_n,
                                        const QStringList& offline_files) {
  QPointer<RepeatabilityTestDialog> self(this);

  auto report_progress = [&](int done, int total, const QString& detail) {
    if (!self) {
      return;
    }
    QMetaObject::invokeMethod(
        self.data(),
        [self, done, total, detail]() {
          if (self) {
            self->OnProgress(done, total, detail);
          }
        },
        Qt::QueuedConnection);
  };

  auto finish = [&](bool ok, const QString& msg) {
    if (!self) {
      return;
    }
    QMetaObject::invokeMethod(
        self.data(),
        [self, ok, msg]() {
          if (self) {
            self->OnFinished(ok, msg);
          }
        },
        Qt::QueuedConnection);
  };

  int ok_cycles = 0;
  int fail_cycles = 0;

  if (mode == Mode::kLive) {
    for (int i = 0; i < live_n; ++i) {
      if (cancel_.load()) {
        finish(true, tr("已停止：完成 %1/%2，成功 %3，失败 %4")
                         .arg(i)
                         .arg(live_n)
                         .arg(ok_cycles)
                         .arg(fail_cycles));
        return;
      }
      report_progress(i, live_n, tr("实机触发中…"));
      visual::CycleResultEvent ev;
      const bool ok = engine_->RunOfflineCycle(station, &ev);
      if (ok) {
        ++ok_cycles;
        AbsorbOkLogs(&extrema_, ev.logs, ResolveDepthPath(station, ev, QString()));
      } else {
        ++fail_cycles;
      }
      report_progress(i + 1, live_n, ok ? tr("本轮成功") : tr("本轮失败"));
    }
    finish(true, tr("实机测试结束：成功 %1，失败 %2").arg(ok_cycles).arg(fail_cycles));
    return;
  }

  const int n = offline_files.size();
  for (int i = 0; i < n; ++i) {
    if (cancel_.load()) {
      finish(true, tr("已停止：完成 %1/%2，成功 %3，失败 %4")
                       .arg(i)
                       .arg(n)
                       .arg(ok_cycles)
                       .arg(fail_cycles));
      return;
    }
    const QString depth = offline_files[i];
    report_progress(i, n, QFileInfo(depth).fileName());
    visual::CycleResultEvent ev;
    const bool ok = engine_->RunReplayDepthFile(station, depth.toStdString(), &ev);
    if (ok) {
      ++ok_cycles;
      AbsorbOkLogs(&extrema_, ev.logs, depth);
    } else {
      ++fail_cycles;
    }
    report_progress(i + 1, n, ok ? tr("本轮成功") : tr("本轮失败"));
  }
  finish(true, tr("离线测试结束：成功 %1，失败 %2").arg(ok_cycles).arg(fail_cycles));
}

void RepeatabilityTestDialog::FillResultTable() {
  const int n_log = static_cast<int>(visual::kLogCountPerStation);
  // 极差 / 最大 / 最小 各 n_log 行
  result_table_->setRowCount(n_log * 3);

  const QColor c_delta = ColorDelta();
  const QColor c_max = ColorMax();
  const QColor c_min = ColorMin();

  auto put_pair = [&](int row, int field_idx, bool has, double v, const QString& path,
                      const QColor& color) {
    const int col_v = 1 + field_idx * 2;
    const int col_p = col_v + 1;
    result_table_->setItem(row, col_v, MakeTextItem(FmtNum(has, v), color));
    result_table_->setItem(row, col_p, MakeTextItem(has ? path : QStringLiteral("—"), color));
  };

  // 每个 Log 一行极差：该 Log 各字段 (max - min)；路径列无对应单文件，填 —
  for (std::size_t li = 0; li < extrema_.size(); ++li) {
    const int row = static_cast<int>(li);
    result_table_->setItem(row, 0, MakeTextItem(tr("Log%1 极差").arg(li + 1), c_delta));
    for (int f = 0; f < kFieldCount; ++f) {
      const auto* fe = FieldAt(extrema_[li], f);
      const bool has = fe != nullptr && fe->has_max && fe->has_min;
      put_pair(row, f, has, has ? (fe->max_v - fe->min_v) : 0.0, QString(), c_delta);
    }
  }

  for (std::size_t li = 0; li < extrema_.size(); ++li) {
    const int row = n_log + static_cast<int>(li);
    result_table_->setItem(row, 0, MakeTextItem(tr("Log%1 最大").arg(li + 1), c_max));
    for (int f = 0; f < kFieldCount; ++f) {
      const auto* fe = FieldAt(extrema_[li], f);
      put_pair(row, f, fe->has_max, fe->max_v, fe->max_path, c_max);
    }
  }
  for (std::size_t li = 0; li < extrema_.size(); ++li) {
    const int row = n_log * 2 + static_cast<int>(li);
    result_table_->setItem(row, 0, MakeTextItem(tr("Log%1 最小").arg(li + 1), c_min));
    for (int f = 0; f < kFieldCount; ++f) {
      const auto* fe = FieldAt(extrema_[li], f);
      put_pair(row, f, fe->has_min, fe->min_v, fe->min_path, c_min);
    }
  }
}
