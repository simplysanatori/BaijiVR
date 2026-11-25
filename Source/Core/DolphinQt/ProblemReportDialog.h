// Copyright 2026 AzaharFrame contributors
// Licensed under GPLv2 or any later version
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <atomic>
#include <memory>

#include <QDialog>

#include "UICommon/ProblemReport.h"

class QLabel;
class QPlainTextEdit;
class QPushButton;
class QThread;

// Help > Report a Problem, and what the start after a run that did not end cleanly shows. See What's
// Sent shows all of it, Save Report makes a zip. Send Report is only in a build with an address to
// send to, and nothing is sent without it.
class ProblemReportDialog final : public QDialog
{
  Q_OBJECT

public:
  ProblemReportDialog(QWidget* parent, ProblemReport::Report report);
  ~ProblemReportDialog() override;

  void reject() override;

private:
  ProblemReport::Report GetReport() const;
  void ShowWhatsSent();
  void Save();
  void Send();
  void OnSent();
  void SetBusy(bool busy, const QString& status);
  void UpdateSend();

  ProblemReport::Report m_report;
  bool m_manual;
  bool m_busy = false;
  std::shared_ptr<std::atomic<bool>> m_cancel;
  QThread* m_thread = nullptr;
  // The thread's, until it is done.
  QString m_send_error;
  QString m_sent_id;

  QPlainTextEdit* m_note;
  QLabel* m_status;
  QPushButton* m_see;
  QPushButton* m_dont;
  QPushButton* m_save;
  QPushButton* m_send = nullptr;
};
