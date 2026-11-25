// Copyright 2026 AzaharFrame contributors
// Licensed under GPLv2 or any later version
// SPDX-License-Identifier: GPL-2.0-or-later

#include "DolphinQt/ProblemReportDialog.h"

#include <ctime>
#include <string>
#include <utility>

#include <QDialogButtonBox>
#include <QDir>
#include <QFileInfo>
#include <QFontDatabase>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QThread>
#include <QVBoxLayout>

#include "Common/Logging/Log.h"

#include "DolphinQt/QtUtils/ModalMessageBox.h"

#include "UICommon/ProblemReportSend.h"

namespace
{
constexpr int MOST_NOTE = 500;

QLabel* MakeText(const QString& text, Qt::Alignment alignment = Qt::AlignHCenter)
{
  auto* const label = new QLabel(text);
  label->setTextFormat(Qt::PlainText);
  label->setWordWrap(true);
  label->setAlignment(alignment);
  return label;
}

QLabel* MakeHeadline(const QString& text)
{
  QLabel* const label = MakeText(text);
  QFont font = label->font();
  font.setPointSizeF(font.pointSizeF() * 1.4);
  label->setFont(font);
  return label;
}
}  // namespace

ProblemReportDialog::ProblemReportDialog(QWidget* parent, ProblemReport::Report report)
    : QDialog(parent), m_report(std::move(report)),
      m_manual(m_report.kind == ProblemReport::Kind::Manual)
{
  setWindowTitle(QString::fromStdString(m_report.headline));

  auto* const layout = new QVBoxLayout(this);
  layout->addWidget(MakeHeadline(QString::fromStdString(m_report.headline)));
  // (The headline says it for the rest.)
  if (!m_manual && m_report.kind != ProblemReport::Kind::Unexpected)
    layout->addWidget(MakeText(tr("Reason: %1").arg(QString::fromStdString(m_report.reason))));
  if (!m_report.title.empty())
    layout->addWidget(MakeText(tr("Game: %1").arg(QString::fromStdString(m_report.title))));

  auto* const label =
      MakeText(m_manual ? tr("What Happened?") : tr("What Were You Doing?"), Qt::AlignLeft);
  layout->addWidget(label);
  m_note = new QPlainTextEdit;
  m_note->setTabChangesFocus(true);
  m_note->setMinimumHeight(m_note->fontMetrics().lineSpacing() * 3 + 16);
  label->setBuddy(m_note);
  layout->addWidget(m_note, 1);
  connect(m_note, &QPlainTextEdit::textChanged, this, [this] {
    if (m_note->toPlainText().size() > MOST_NOTE)
    {
      m_note->setPlainText(m_note->toPlainText().left(MOST_NOTE));
      m_note->moveCursor(QTextCursor::End);
    }
    UpdateSend();
  });

  m_status = MakeText({});
  m_status->hide();
  layout->addWidget(m_status);

  const bool can_send = ProblemReport::CanSend();
  auto* const buttons = new QDialogButtonBox;
  // (With nothing to send there is no question of sending.)
  m_dont = buttons->addButton(m_manual || !can_send ? tr("Cancel") : tr("Don't Send"),
                              QDialogButtonBox::RejectRole);
  m_see = buttons->addButton(tr("See What's Sent"), QDialogButtonBox::ResetRole);
  m_save = buttons->addButton(tr("Save Report"), QDialogButtonBox::ActionRole);
  if (can_send)
    m_send = buttons->addButton(tr("Send Report"), QDialogButtonBox::ActionRole);
  for (QPushButton* const button : {m_see, m_save, m_send})
  {
    if (button)
      button->setAutoDefault(false);
  }
  // Nothing goes by accident.
  m_dont->setDefault(true);
  layout->addWidget(buttons);
  connect(m_dont, &QPushButton::clicked, this, &ProblemReportDialog::reject);
  connect(m_see, &QPushButton::clicked, this, &ProblemReportDialog::ShowWhatsSent);
  connect(m_save, &QPushButton::clicked, this, &ProblemReportDialog::Save);
  if (m_send)
    connect(m_send, &QPushButton::clicked, this, &ProblemReportDialog::Send);

  QWidget* last = m_dont;
  for (QWidget* const next : std::initializer_list<QWidget*>{m_note, m_see, m_save, m_send})
  {
    if (!next)
      continue;
    setTabOrder(last, next);
    last = next;
  }
  m_dont->setFocus();

  setMinimumWidth(520);
  UpdateSend();
}

ProblemReportDialog::~ProblemReportDialog()
{
  if (!m_thread)
    return;
  *m_cancel = true;
  m_thread->wait();
  delete m_thread;
}

void ProblemReportDialog::reject()
{
  if (m_thread)
  {
    // Gives up the sending, and stays.
    *m_cancel = true;
    return;
  }
  QDialog::reject();
}

ProblemReport::Report ProblemReportDialog::GetReport() const
{
  ProblemReport::Report report = m_report;
  ProblemReport::SetNote(&report, m_note->toPlainText().toStdString(),
                         ProblemReport::Scrubber::ForThisMachine());
  return report;
}

void ProblemReportDialog::ShowWhatsSent()
{
  auto* const dialog = new QDialog(this);
  dialog->setAttribute(Qt::WA_DeleteOnClose);
  dialog->setWindowTitle(tr("What's Sent"));
  auto* const layout = new QVBoxLayout(dialog);
  layout->addWidget(MakeText(
      (m_send ? tr("Send Report sends this text and the logs under it, and nothing else. Save "
                   "Report puts the same files in a zip.") :
                tr("Save Report puts this text and the logs under it in a zip. Nothing is sent.")) +
      QLatin1Char(' ') +
      tr("The logs can name your games and their folders. Your home folder shows as ~.")));
  auto* const text = new QPlainTextEdit;
  text->setReadOnly(true);
  text->setLineWrapMode(QPlainTextEdit::NoWrap);
  text->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
  text->setPlainText(QString::fromStdString(ProblemReport::FullText(GetReport())));
  layout->addWidget(text, 1);
  auto* const buttons = new QDialogButtonBox(QDialogButtonBox::Close);
  connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::reject);
  layout->addWidget(buttons);
  buttons->button(QDialogButtonBox::Close)->setFocus();
  dialog->resize(760, 520);
  dialog->open();
}

void ProblemReportDialog::Save()
{
  std::string error;
  const std::string path = ProblemReport::SaveZip(GetReport(), ProblemReport::GetSaveDir(),
                                                  std::time(nullptr), &error);
  if (path.empty())
  {
    ERROR_LOG_FMT(COMMON, "The problem report was not saved: {}", error);
    SetBusy(false, tr("Couldn't Save the Report: %1").arg(QString::fromStdString(error)));
    return;
  }
  const QFileInfo file(QString::fromStdString(path));
  QString folder = file.path();
  if (const QString home = QDir::homePath(); folder.startsWith(home + QLatin1Char('/')))
    folder = QLatin1Char('~') + folder.mid(home.size());
  QWidget* const parent = parentWidget();
  accept();
  ModalMessageBox::information(parent, tr("Report Saved"),
                               tr("Saved in %1 as %2").arg(folder, file.fileName()));
}

void ProblemReportDialog::Send()
{
  SetBusy(true, tr("Sending..."));
  m_cancel = std::make_shared<std::atomic<bool>>(false);
  m_thread = QThread::create([this, report = GetReport(), cancel = m_cancel] {
    const ProblemReport::SendResult result =
        ProblemReport::Send(report, ProblemReport::GetBuiltInDsn(), cancel.get());
    m_send_error = QString::fromStdString(result.error);
    m_sent_id = QString::fromStdString(result.event_id);
  });
  connect(m_thread, &QThread::finished, this, &ProblemReportDialog::OnSent);
  m_thread->start();
}

void ProblemReportDialog::OnSent()
{
  std::exchange(m_thread, nullptr)->deleteLater();
  if (!m_sent_id.isEmpty())
  {
    NOTICE_LOG_FMT(COMMON, "Problem report sent: {}", m_sent_id.toStdString());
    QWidget* const parent = parentWidget();
    const QString id = m_sent_id.left(8);
    accept();
    ModalMessageBox::information(parent, tr("Report Sent"),
                                 tr("Thank you. Report ID: %1").arg(id));
    return;
  }
  WARN_LOG_FMT(COMMON, "The problem report was not sent: {}", m_send_error.toStdString());
  SetBusy(false, *m_cancel ? QString() :
                             tr("Couldn't Send the Report: %1. You can save it instead.")
                                 .arg(m_send_error));
}

void ProblemReportDialog::SetBusy(bool busy, const QString& status)
{
  m_busy = busy;
  // Cancel and Don't Send give up the sending.
  for (QPushButton* const button : {m_see, m_save, m_send})
  {
    if (button)
      button->setEnabled(!busy);
  }
  m_note->setEnabled(!busy);
  m_status->setText(status);
  m_status->setVisible(!status.isEmpty());
  UpdateSend();
}

void ProblemReportDialog::UpdateSend()
{
  if (m_send && m_manual && !m_busy)
    m_send->setEnabled(!m_note->toPlainText().trimmed().isEmpty());
}
