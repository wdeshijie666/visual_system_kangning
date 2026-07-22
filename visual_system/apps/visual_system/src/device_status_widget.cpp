#include "device_status_widget.h"

#include <QHBoxLayout>
#include <QFormLayout>
#include <QStringList>
#include <QVBoxLayout>

#include "visual/app_context.h"

namespace {

constexpr const char* kOkLineStyle = "";
constexpr const char* kFaultLineStyle = "color: #e74c3c;";
constexpr const char* kLightGreenStyle =
    "border-radius: 9px; background-color: #2ecc71; border: 1px solid #27ae60;";
constexpr const char* kLightRedStyle =
    "border-radius: 9px; background-color: #e74c3c; border: 1px solid #c0392b;";

}  // namespace

DeviceStatusWidget::DeviceStatusWidget(QWidget* parent) : QWidget(parent) {
  auto* layout = new QVBoxLayout(this);
  auto* group = new QGroupBox(tr("设备信息"), this);
  auto* form = new QFormLayout(group);
  plc_label_ = new QLabel(tr("PLC: 未连接"), group);
  algo_label_ = new QLabel(tr("算法: 未启用"), group);
  cam_r05_label_ = new QLabel(tr("R05 相机: 未知"), group);
  cam_r09_label_ = new QLabel(tr("R09 相机: 未知"), group);
  form->addRow(plc_label_);
  form->addRow(algo_label_);
  form->addRow(cam_r05_label_);
  form->addRow(cam_r09_label_);
  layout->addWidget(group);

  auto* overall_row = new QWidget(this);
  auto* overall_layout = new QHBoxLayout(overall_row);
  overall_layout->setContentsMargins(8, 4, 8, 4);
  overall_light_ = new QLabel(overall_row);
  overall_light_->setFixedSize(18, 18);
  overall_text_ = new QLabel(tr("总状态: 异常"), overall_row);
  overall_layout->addWidget(overall_light_, 0, Qt::AlignVCenter);
  overall_layout->addWidget(overall_text_, 0, Qt::AlignVCenter);
  overall_layout->addStretch(1);
  layout->addWidget(overall_row);
  layout->addStretch(1);

  for (const auto& kv : visual::AppContext::Instance().Devices()) {
    camera_ok_.insert(QString::fromStdString(kv.second.id), false);
  }
  RefreshStatusPresentation();
}

void DeviceStatusWidget::SetLineStyle(QLabel* label, bool ok) {
  if (label == nullptr) {
    return;
  }
  label->setStyleSheet(ok ? QString::fromUtf8(kOkLineStyle) : QString::fromUtf8(kFaultLineStyle));
}

void DeviceStatusWidget::RefreshStatusPresentation() {
  const bool plc_line_ok = plc_ok_ && plc_heartbeat_ok_;
  SetLineStyle(plc_label_, plc_line_ok);
  SetLineStyle(algo_label_, algo_ok_);
  const auto& settings = visual::AppContext::Instance().Settings();
  SetLineStyle(cam_r05_label_, !settings.station_r05.enabled || cam_r05_ok_);
  SetLineStyle(cam_r09_label_, !settings.station_r09.enabled || cam_r09_ok_);

  const bool overall_ok = IsOverallOk();
  if (overall_light_ != nullptr) {
    overall_light_->setStyleSheet(
        overall_ok ? QString::fromUtf8(kLightGreenStyle) : QString::fromUtf8(kLightRedStyle));
  }
  if (overall_text_ != nullptr) {
    overall_text_->setText(overall_ok ? tr("总状态: 正常") : tr("总状态: 异常"));
    overall_text_->setStyleSheet(overall_ok ? QString() : QString::fromUtf8(kFaultLineStyle));
  }
}

void DeviceStatusWidget::SetPlcStatus(bool connected, bool heartbeat) {
  plc_ok_ = connected;
  plc_heartbeat_ok_ = heartbeat;
  if (!connected) {
    plc_label_->setText(tr("PLC: 未连接"));
  } else if (!heartbeat) {
    plc_label_->setText(tr("PLC: 已连接(心跳异常)"));
  } else {
    plc_label_->setText(tr("PLC: 已连接"));
  }
  RefreshStatusPresentation();
}

void DeviceStatusWidget::SetAlgoStatus(bool running, const QString& detail) {
  algo_ok_ = running;
  const QString state = running ? tr("运行中") : tr("未运行");
  if (detail.isEmpty()) {
    algo_label_->setText(tr("算法: %1").arg(state));
  } else {
    algo_label_->setText(tr("算法: %1 (%2)").arg(state, detail));
  }
  RefreshStatusPresentation();
}

void DeviceStatusWidget::SetCameraStatus(const QString& id, bool connected) {
  camera_ok_.insert(id, connected);
  const QString text = connected ? tr("已连接") : tr("未连接");

  QString station;
  const auto& devices = visual::AppContext::Instance().Devices();
  const auto it = devices.find(id.toStdString());
  if (it != devices.end()) {
    station = QString::fromStdString(it->second.station);
  }

  const bool is_r05 = station.compare(QStringLiteral("r05"), Qt::CaseInsensitive) == 0 ||
                      id.contains(QStringLiteral("r05"), Qt::CaseInsensitive) ||
                      (station.isEmpty() && (id.contains(QStringLiteral("01")) ||
                                             id.contains(QStringLiteral("rvc_01"), Qt::CaseInsensitive)));

  const auto& settings = visual::AppContext::Instance().Settings();
  const bool station_enabled = is_r05 ? settings.station_r05.enabled : settings.station_r09.enabled;
  const QString display =
      station_enabled ? text : tr("已禁用(%1)").arg(text);

  if (is_r05) {
    cam_r05_ok_ = !station_enabled || connected;
    cam_r05_label_->setText(tr("R05 相机 (%1): %2").arg(id, display));
  } else {
    cam_r09_ok_ = !station_enabled || connected;
    cam_r09_label_->setText(tr("R09 相机 (%1): %2").arg(id, display));
  }
  RefreshStatusPresentation();
}

bool DeviceStatusWidget::AreCamerasOk() const {
  if (camera_ok_.isEmpty()) {
    return false;
  }
  const auto& settings = visual::AppContext::Instance().Settings();
  const auto& devices = visual::AppContext::Instance().Devices();
  bool any_enabled = false;
  for (auto it = camera_ok_.constBegin(); it != camera_ok_.constEnd(); ++it) {
    const auto dit = devices.find(it.key().toStdString());
    bool station_enabled = true;
    if (dit != devices.end()) {
      station_enabled = (dit->second.station == "r09" || dit->second.station == "R09")
                            ? settings.station_r09.enabled
                            : settings.station_r05.enabled;
    }
    if (!station_enabled) {
      continue;
    }
    any_enabled = true;
    if (!it.value()) {
      return false;
    }
  }
  return any_enabled;
}

bool DeviceStatusWidget::IsOverallOk() const {
  return plc_ok_ && plc_heartbeat_ok_ && algo_ok_ && AreCamerasOk();
}

QString DeviceStatusWidget::DescribeFaults() const {
  QStringList faults;
  if (!plc_ok_) {
    faults << tr("PLC未连接");
  } else if (!plc_heartbeat_ok_) {
    faults << tr("PLC心跳异常");
  }
  if (!algo_ok_) {
    faults << tr("算法服务异常");
  }
  const auto& settings = visual::AppContext::Instance().Settings();
  const auto& devices = visual::AppContext::Instance().Devices();
  for (auto it = camera_ok_.constBegin(); it != camera_ok_.constEnd(); ++it) {
    if (!it.value()) {
      const auto dit = devices.find(it.key().toStdString());
      bool station_enabled = true;
      if (dit != devices.end()) {
        station_enabled = (dit->second.station == "r09" || dit->second.station == "R09")
                              ? settings.station_r09.enabled
                              : settings.station_r05.enabled;
      }
      if (!station_enabled) {
        continue;
      }
      faults << tr("相机未连接(%1)").arg(it.key());
    }
  }
  return faults.join(QStringLiteral("；"));
}
