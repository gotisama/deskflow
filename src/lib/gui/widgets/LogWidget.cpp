/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2025 - 2026 Chris Rizzitello <sithlord48@gmail.com>
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "LogWidget.h"
#include "common/PlatformInfo.h"

#include <gui/Logger.h>

#include <QFont>
#include <QPlainTextEdit>
#include <QScrollBar>
#include <QVBoxLayout>

LogWidget::LogWidget(QWidget *parent) : QWidget{parent}, m_textLog{new QPlainTextEdit(this)}
{
  m_textLog->setReadOnly(true);
  m_textLog->setMaximumBlockCount(10000);
  m_textLog->setLineWrapMode(QPlainTextEdit::NoWrap);

  // setup the log font
  //
  // Don't use QFontDatabase::systemFont(FixedFont): on Windows it historically
  // resolves to "Fixedsys", a legacy raster font. Qt 6.10's DirectWrite-based
  // font engine cannot load raster fonts via CreateFontFaceFromHDC() and
  // dereferences a null face on the failure path, crashing Qt6Gui.dll with
  // c0000005 during MainWindow construction. Pick a TrueType monospace family
  // explicitly and let Qt fall back via the Monospace style hint.
  QFont logFont;
  logFont.setFamilies({"Cascadia Mono", "Consolas", "Menlo", "DejaVu Sans Mono", "Liberation Mono", "Monospace"});
  logFont.setStyleHint(QFont::Monospace);
  logFont.setFixedPitch(true);
  m_textLog->setFont(logFont);
  if (deskflow::platform::isMac()) {
    auto f = m_textLog->font();
    f.setPixelSize(12);
    m_textLog->setFont(f);
  }

  auto layout = new QVBoxLayout;
  layout->setContentsMargins(0, 0, 0, 0);
  layout->addWidget(m_textLog);

  setLayout(layout);

  connect(
      deskflow::gui::Logger::instance(), &deskflow::gui::Logger::newLine, m_textLog, &QPlainTextEdit::appendPlainText
  );
}

void LogWidget::appendLine(const QString &msg)
{
  m_textLog->appendPlainText(msg);
}

void LogWidget::findNext(const QString &text)
{
  if (text.isEmpty())
    return;

  if (!m_textLog->find(text)) {
    m_textLog->moveCursor(QTextCursor::Start);
    m_textLog->find(text);
  }
}

void LogWidget::findPrevious(const QString &text)
{
  if (text.isEmpty())
    return;

  if (!m_textLog->find(text, QTextDocument::FindBackward)) {
    m_textLog->moveCursor(QTextCursor::End);
    m_textLog->find(text, QTextDocument::FindBackward);
  }
}

void LogWidget::scrollToBottom() const
{
  auto sb = m_textLog->verticalScrollBar();
  sb->setValue(sb->maximum());
}
