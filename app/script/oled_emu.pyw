#!/usr/bin/env python3
"""
Simple LCD20x4 Display Viewer — PySide6
- Receives UDP packets (type='lcd20x4') and displays them like a 20x4 LCD
- Menu: set background color, text color, and font
- Footer: "Copyright 2025 Darkone83 -=- Team Resurgent"
- Defaults: black background, white text
- Settings auto-load/save from lcd_viewer.ini (same folder as script)
"""

import json
import socket
import threading
import sys
import os
from typing import List, Optional
import configparser

from PySide6.QtCore import Qt, QObject, Signal, QSize
from PySide6.QtGui import QAction, QFont, QCloseEvent, QFontMetrics, QIcon
from PySide6.QtWidgets import (
    QApplication,
    QMainWindow,
    QWidget,
    QVBoxLayout,
    QHBoxLayout,
    QLabel,
    QMenuBar,
    QColorDialog,
    QFontDialog,
    QFrame,
    QSizePolicy,
    QSpacerItem,
)

UDP_PORT = 35182
INI_NAME = "lcd_viewer.ini"


# ---------- Worker: UDP receiver on a background thread ----------
class UdpReceiver(QObject):
    packet = Signal(dict)  # emits decoded JSON dicts

    def __init__(self, port: int = UDP_PORT):
        super().__init__()
        self._port = port
        self._stop = False
        self._thread: Optional[threading.Thread] = None

    def start(self):
        if self._thread is not None:
            return
        self._stop = False
        self._thread = threading.Thread(target=self._run, daemon=True)
        self._thread.start()

    def stop(self):
        self._stop = True

    def _run(self):
        sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        try:
            sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
            sock.bind(("", self._port))
            while not self._stop:
                try:
                    sock.settimeout(0.25)
                    data, _addr = sock.recvfrom(4096)
                except socket.timeout:
                    continue
                except OSError:
                    break

                try:
                    msg = json.loads(data.decode("utf-8"))
                except json.JSONDecodeError:
                    continue

                if isinstance(msg, dict) and msg.get("type") == "lcd20x4":
                    self.packet.emit(msg)
        except Exception as e:
            self.packet.emit({"type": "error", "error": str(e)})
        finally:
            try:
                sock.close()
            except Exception:
                pass


# ---------- Main Window ----------
class LCDWindow(QMainWindow):
    def __init__(self):
        super().__init__()
        self.setWindowTitle("Type D OLED Emulator")
        self.setMinimumSize(640, 300)

        # Set window icon (dc.ico in same folder as script)
        icon_path = os.path.join(os.path.dirname(os.path.abspath(sys.argv[0])), "dc.ico")
        if os.path.exists(icon_path):
            self.setWindowIcon(QIcon(icon_path))

        # Defaults (overridden by INI if present)
        self.bg_color = "#000000"  # black
        self.fg_color = "#FFFFFF"  # white
        self.font = QFont("Courier New", 16, QFont.Bold)

        # Auto-load settings from INI
        self._ini_path = os.path.join(os.path.dirname(os.path.abspath(sys.argv[0])), INI_NAME)
        self._load_settings()

        # Central widget & root layout
        self.central = QWidget(self)
        self.setCentralWidget(self.central)

        root_v = QVBoxLayout(self.central)
        root_v.setContentsMargins(16, 16, 12, 12)
        root_v.setSpacing(8)

        # Top spacer to center vertically
        root_v.addItem(QSpacerItem(0, 0, QSizePolicy.Minimum, QSizePolicy.Expanding))

        # --- LCD area wrapper (centered H and V) ---
        wrapper_h = QHBoxLayout()
        wrapper_h.setContentsMargins(0, 0, 0, 0)
        wrapper_h.setSpacing(0)

        # Left spacer to center horizontally
        wrapper_h.addItem(QSpacerItem(0, 0, QSizePolicy.Expanding, QSizePolicy.Minimum))

        # LCD container (no border)
        self.lcd_container = QWidget(self.central)
        self.lcd_container.setAttribute(Qt.WA_StyledBackground, True)
        lcd_v = QVBoxLayout(self.lcd_container)
        lcd_v.setContentsMargins(0, 0, 0, 0)
        lcd_v.setSpacing(4)

        # 4 LCD row labels (exact 20-char width, centered text)
        self.row_labels: List[QLabel] = []
        for _ in range(4):
            lbl = QLabel(" " * 20, self.lcd_container)
            lbl.setAlignment(Qt.AlignHCenter | Qt.AlignVCenter)
            lbl.setFont(self.font)
            lbl.setTextInteractionFlags(Qt.NoTextInteraction)
            lbl.setFrameShape(QFrame.NoFrame)
            lbl.setLineWidth(0)
            lbl.setAttribute(Qt.WA_StyledBackground, True)
            h = self._row_height_for_font(self.font)
            lbl.setFixedHeight(h)
            lbl.setSizePolicy(QSizePolicy.Fixed, QSizePolicy.Fixed)
            self.row_labels.append(lbl)
            lcd_v.addWidget(lbl, 0, Qt.AlignHCenter)

        # Add the centered LCD container
        wrapper_h.addWidget(self.lcd_container, 0, Qt.AlignCenter)

        # Right spacer to center horizontally
        wrapper_h.addItem(QSpacerItem(0, 0, QSizePolicy.Expanding, QSizePolicy.Minimum))

        # Put wrapper into root
        root_v.addLayout(wrapper_h)

        # Footer (centered)
        self.footer = QLabel("Copyright 2025 Darkone83 -=- Team Resurgent", self.central)
        self.footer.setAlignment(Qt.AlignHCenter | Qt.AlignVCenter)
        root_v.addWidget(self.footer, 0, Qt.AlignHCenter)

        # Bottom spacer to center vertically
        root_v.addItem(QSpacerItem(0, 0, QSizePolicy.Minimum, QSizePolicy.Expanding))

        # Apply initial colors/styles and geometry
        self.apply_colors()
        self._update_row_fixed_widths()

        # Menu
        self._build_menu()

        # UDP receiver
        self.rx = UdpReceiver(UDP_PORT)
        self.rx.packet.connect(self.on_packet)
        self.rx.start()

    # ----- Settings: INI load/save -----
    def _load_settings(self):
        cfg = configparser.ConfigParser()
        if os.path.exists(self._ini_path):
            try:
                cfg.read(self._ini_path)
                if "Display" in cfg:
                    self.bg_color = cfg["Display"].get("bg_color", self.bg_color)
                    self.fg_color = cfg["Display"].get("fg_color", self.fg_color)
                if "Font" in cfg:
                    fam = cfg["Font"].get("family", self.font.family())
                    size = cfg["Font"].getint("size", self.font.pointSize())
                    weight = cfg["Font"].getint("weight", self.font.weight())
                    italic = cfg["Font"].getboolean("italic", self.font.italic())
                    f = QFont(fam, size, weight)
                    f.setItalic(italic)
                    self.font = f
            except Exception:
                pass  # ignore bad INI and keep defaults

    def _save_settings(self):
        cfg = configparser.ConfigParser()
        cfg["Display"] = {
            "bg_color": self.bg_color,
            "fg_color": self.fg_color,
        }
        cfg["Font"] = {
            "family": self.font.family(),
            "size": str(self.font.pointSize()),
            "weight": str(self.font.weight()),
            "italic": "true" if self.font.italic() else "false",
        }
        try:
            with open(self._ini_path, "w", encoding="utf-8") as f:
                cfg.write(f)
        except Exception:
            pass

    # ----- UI / Menu -----
    def _build_menu(self):
        menubar: QMenuBar = self.menuBar()
        settings_menu = menubar.addMenu("&Settings")

        act_bg = QAction("Set &Background Color…", self)
        act_bg.triggered.connect(self.choose_background)
        settings_menu.addAction(act_bg)

        act_fg = QAction("Set &Text Color…", self)
        act_fg.triggered.connect(self.choose_text)
        settings_menu.addAction(act_fg)

        act_font = QAction("Set &Font…", self)
        act_font.triggered.connect(self.choose_font)
        settings_menu.addAction(act_font)

        settings_menu.addSeparator()
        act_reset = QAction("&Reset to Defaults", self)
        act_reset.triggered.connect(self.reset_defaults)
        settings_menu.addAction(act_reset)

    def apply_colors(self):
        # Backgrounds (no borders)
        self.central.setStyleSheet(f"background-color: {self.bg_color};")
        self.lcd_container.setStyleSheet(f"background-color: {self.bg_color}; border: none;")
        for lbl in self.row_labels:
            lbl.setStyleSheet(f"color: {self.fg_color}; background-color: {self.bg_color}; border: none;")
            lbl.setFont(self.font)
        self.footer.setStyleSheet(f"color: {self.fg_color}; background-color: {self.bg_color};")

    def choose_background(self):
        color = QColorDialog.getColor(self.bg_color, self, "Choose LCD Background Color")
        if color.isValid():
            self.bg_color = color.name()
            self.apply_colors()
            self._save_settings()

    def choose_text(self):
        color = QColorDialog.getColor(self.fg_color, self, "Choose LCD Text Color")
        if color.isValid():
            self.fg_color = color.name()
            self.apply_colors()
            self._save_settings()

    def choose_font(self):
        ok, font = QFontDialog.getFont(self.font, self, "Choose LCD Font")
        if ok:
            self.font = font
            # Update styles & geometry tied to the font
            self.apply_colors()
            self._update_row_fixed_widths()
            self._save_settings()

    def reset_defaults(self):
        self.bg_color = "#000000"
        self.fg_color = "#FFFFFF"
        self.font = QFont("Courier New", 16, QFont.Bold)
        self.apply_colors()
        self._update_row_fixed_widths()
        self._save_settings()

    # ----- Geometry helpers -----
    def _row_height_for_font(self, font: QFont) -> int:
        fm = QFontMetrics(font)
        return fm.height() + 4

    def _row_width_for_font(self, font: QFont) -> int:
        fm = QFontMetrics(font)
        char_w = fm.averageCharWidth() or fm.horizontalAdvance("M")
        return int(char_w * 20) + 8

    def _update_row_fixed_widths(self):
        """Recompute the fixed width/height of each label based on current font."""
        w = self._row_width_for_font(self.font)
        h = self._row_height_for_font(self.font)
        for lbl in self.row_labels:
            lbl.setFixedSize(QSize(w, h))

    # ----- Data handling -----
    def on_packet(self, msg: dict):
        if not isinstance(msg, dict):
            return
        if msg.get("type") == "error":
            return  # silent

        rows = msg.get("rows", [])
        if not isinstance(rows, list):
            rows = []

        for i in range(4):
            if i < len(rows) and isinstance(rows[i], str):
                text = rows[i][:20].ljust(20)
            else:
                text = " " * 20
            self.row_labels[i].setText(text)

    # ----- Lifecycle -----
    def resizeEvent(self, event):
        self._update_row_fixed_widths()
        super().resizeEvent(event)

    def closeEvent(self, event: QCloseEvent) -> None:
        if hasattr(self, "rx") and self.rx is not None:
            self.rx.stop()
        self._save_settings()
        return super().closeEvent(event)


def main():
    app = QApplication(sys.argv)

    # Set app icon for taskbar/dock (dc.ico in same folder as script)
    icon_path = os.path.join(os.path.dirname(os.path.abspath(sys.argv[0])), "dc.ico")
    if os.path.exists(icon_path):
        app.setWindowIcon(QIcon(icon_path))

    win = LCDWindow()
    win.show()
    sys.exit(app.exec())


if __name__ == "__main__":
    main()
