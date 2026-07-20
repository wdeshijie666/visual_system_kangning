#pragma once

#include <QGroupBox>
#include <QHash>
#include <QLabel>
#include <QWidget>

class DeviceStatusWidget : public QWidget {
  Q_OBJECT
 public:
  explicit DeviceStatusWidget(QWidget* parent = nullptr);
  void SetPlcStatus(bool connected);
  void SetCameraStatus(const QString& id, bool connected);
  void SetAlgoStatus(bool running, const QString& detail);

  bool IsPlcOk() const { return plc_ok_; }
  bool IsAlgoOk() const { return algo_ok_; }
  /** 配置中的全部相机均已连接。 */
  bool AreCamerasOk() const;
  /** PLC / 相机 / 算法全部正常。 */
  bool IsOverallOk() const;
  /** 汇总当前异常项（中文），全部正常返回空串。 */
  QString DescribeFaults() const;

 private:
  void RefreshStatusPresentation();
  void SetLineStyle(QLabel* label, bool ok);

  QLabel* plc_label_ = nullptr;
  QLabel* algo_label_ = nullptr;
  QLabel* cam_r05_label_ = nullptr;
  QLabel* cam_r09_label_ = nullptr;
  QLabel* overall_light_ = nullptr;
  QLabel* overall_text_ = nullptr;
  bool plc_ok_ = false;
  bool algo_ok_ = false;
  bool cam_r05_ok_ = false;
  bool cam_r09_ok_ = false;
  QHash<QString, bool> camera_ok_;
};
