#include "theme.h"

QString theme::StyleSheet() {
  return QString(R"(
QMainWindow, QWidget { background: %1; color: %2; }

QGroupBox {
  background: %3;
  border: 1px solid %4;
  border-radius: 6px;
  margin-top: 14px;
  padding: 10px 8px 8px 8px;
  font-size: 10px;
  font-weight: bold;
  color: %5;
}
QGroupBox::title {
  subcontrol-origin: margin;
  subcontrol-position: top left;
  left: 10px;
  padding: 0 4px;
  letter-spacing: 1px;
}

QPushButton {
  background: #232830;
  border: 1px solid %4;
  border-radius: 5px;
  padding: 8px 10px;
  color: %2;
  font-weight: 600;
}
QPushButton:hover  { background: #2b313a; border-color: #3d4653; }
QPushButton:pressed{ background: #1a1e24; }
QPushButton:checked{ background: %6; border-color: %6; color: #08121f; }
QPushButton:disabled { color: #4a525c; border-color: #232830; }

QSlider::groove:horizontal {
  height: 5px; background: #10131700; border-radius: 3px;
  background-color: #262c34;
}
QSlider::sub-page:horizontal { background: %6; border-radius: 3px; }
QSlider::handle:horizontal {
  background: #d7dee6; width: 14px; margin: -6px 0; border-radius: 7px;
}

QStatusBar { color: %5; border-top: 1px solid %4; }
QStatusBar::item { border: none; }
QLabel { color: %2; background: transparent; }
)")
      .arg(theme::kBg, theme::kText, theme::kPanel, theme::kEdge, theme::kTextDim,
           theme::kAccent);
}
