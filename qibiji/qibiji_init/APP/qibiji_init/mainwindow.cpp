#include "mainwindow.h"
#include "ui_mainwindow.h"

#include "at4gconfigurator.h"
#include "devicesession.h"

#include <QDateTime>
#include <QFile>
#include <QFileDialog>
#include <QMessageBox>
#include <QRegularExpression>
#include <QSettings>
#include <QTextStream>

namespace {

/* Pull the value out of a module reply such as "+NAME:BT24\r\nOK" / "BT24". */
QString bleReplyValue(const QString &raw, const QString &tag)
{
    const QStringList lines =
        raw.split(QRegularExpression(QStringLiteral("[\\r\\n]+")), Qt::SkipEmptyParts);

    for (const QString &line : lines)
    {
        QString text = line.trimmed();
        if (text.isEmpty())
            continue;
        if (text.compare(QStringLiteral("OK"), Qt::CaseInsensitive) == 0)
            continue;
        if (text.compare(QStringLiteral("ERROR"), Qt::CaseInsensitive) == 0)
            continue;
        if (text.startsWith(QStringLiteral("AT+"), Qt::CaseInsensitive))
            continue; /* echo of the command we sent */

        const QString colon = QLatin1Char('+') + tag + QLatin1Char(':');
        const QString equal = QLatin1Char('+') + tag + QLatin1Char('=');
        if (text.startsWith(colon, Qt::CaseInsensitive))
            text = text.mid(colon.size()).trimmed();
        else if (text.startsWith(equal, Qt::CaseInsensitive))
            text = text.mid(equal.size()).trimmed();

        return text;
    }
    return QString();
}

/* First MAC-like token in the reply, normalised to AA:BB:CC:DD:EE:FF. */
QString bleMacOf(const QString &raw)
{
    /* DX-BT24 answers AT+LADDR with "+LADDR=48872D8429CB" (no separators);
     * other firmware revisions may use AA:BB:.. or AA-BB-.. */
    static const QRegularExpression separated(
        QStringLiteral("([0-9A-Fa-f]{2}[-:]){5}[0-9A-Fa-f]{2}"));
    static const QRegularExpression bare(
        QStringLiteral("(?<![0-9A-Fa-f])[0-9A-Fa-f]{12}(?![0-9A-Fa-f])"));

    const QRegularExpressionMatch match = separated.match(raw);
    if (match.hasMatch())
    {
        QString mac = match.captured(0);
        mac.replace(QLatin1Char('-'), QLatin1Char(':'));
        return mac.toUpper();
    }

    const QRegularExpressionMatch bareMatch = bare.match(raw);
    if (bareMatch.hasMatch())
    {
        const QString hex = bareMatch.captured(0).toUpper();
        QString mac;
        for (int i = 0; i < 12; i += 2)
        {
            if (i > 0)
                mac += QLatin1Char(':');
            mac += hex.mid(i, 2);
        }
        return mac;
    }

    return QString();
}

QString toPrintable(const QByteArray &data)
{
    QString text;
    for (int i = 0; i < data.size(); ++i)
    {
        const quint8 c = static_cast<quint8>(data.at(i));
        if (c >= 0x20u && c < 0x7Fu)
            text.append(QChar(c));
        else
            text.append(QStringLiteral("\\x%1").arg(c, 2, 16, QLatin1Char('0')).toUpper());
    }
    return text;
}

QString hexText(const QByteArray &data)
{
    return QString::fromLatin1(data.toHex(' ')).toUpper();
}

} // namespace

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    ui->setupUi(this);

    m_session = new DeviceSession(this);
    m_config = new At4gConfigurator(m_session, this);
    connect(m_config, &At4gConfigurator::progress, this, &MainWindow::on4gProgress);
    connect(m_config, &At4gConfigurator::finished, this, &MainWindow::on4gFinished);
    connect(m_config, &At4gConfigurator::logLine, this, &MainWindow::on4gLogLine);

    connect(ui->btnRefresh, &QPushButton::clicked, this, &MainWindow::onRefreshPorts);
    connect(ui->btnOpen, &QPushButton::clicked, this, &MainWindow::onToggleOpen);
    connect(ui->btnProbe, &QPushButton::clicked, this, &MainWindow::onProbe);
    connect(ui->btnRead, &QPushButton::clicked, this, &MainWindow::onReadConfig);
    connect(ui->btnWrite, &QPushButton::clicked, this, &MainWindow::onWriteConfig);
    connect(ui->btn4gPush, &QPushButton::clicked, this, &MainWindow::on4gPush);
    connect(ui->btn4gRead, &QPushButton::clicked, this, &MainWindow::on4gRead);
    connect(ui->btn4gStop, &QPushButton::clicked, this, &MainWindow::on4gStop);
    connect(ui->btnAtSend, &QPushButton::clicked, this, &MainWindow::onSendAt);
    connect(ui->lineAtCmd, &QLineEdit::returnPressed, this, &MainWindow::onSendAt);
    connect(ui->btnClearLog, &QPushButton::clicked, this, &MainWindow::onClearLog);
    connect(ui->btnClearAt, &QPushButton::clicked, this, &MainWindow::onClearAtLog);
    connect(ui->btnClear4g, &QPushButton::clicked, this, &MainWindow::onClear4gLog);
    connect(ui->btnExportLog, &QPushButton::clicked, this, &MainWindow::onExportLog);

    connect(ui->btnBleAddr, &QPushButton::clicked, this, &MainWindow::onBleReadAddr);
    connect(ui->btnBleName, &QPushButton::clicked, this, &MainWindow::onBleReadName);
    connect(ui->btnBleVersion, &QPushButton::clicked, this, &MainWindow::onBleReadVersion);
    connect(ui->btnBleSend, &QPushButton::clicked, this, &MainWindow::onBleSend);
    connect(ui->lineBleCmd, &QLineEdit::returnPressed, this, &MainWindow::onBleSend);
    connect(ui->btnBleClearLog, &QPushButton::clicked, this, &MainWindow::onBleClearLog);
    connect(ui->lineOtaFlag, &QLineEdit::textChanged, this, &MainWindow::updateOtaFlagHex);

    connect(ui->btnAtCsq, &QPushButton::clicked, this, [this]() {
        ui->lineAtCmd->setText(QStringLiteral("usr.cn#AT+CSQ?"));
        onSendAt();
    });
    connect(ui->btnAtIccid, &QPushButton::clicked, this, [this]() {
        ui->lineAtCmd->setText(QStringLiteral("usr.cn#AT+ICCID?"));
        onSendAt();
    });
    connect(ui->btnAtCclk, &QPushButton::clicked, this, [this]() {
        ui->lineAtCmd->setText(QStringLiteral("usr.cn#AT+CCLK?"));
        onSendAt();
    });
    connect(ui->btnAtMqttCid, &QPushButton::clicked, this, [this]() {
        ui->lineAtCmd->setText(QStringLiteral("usr.cn#AT+MQTTCID?"));
        onSendAt();
    });

    connect(m_session, &DeviceSession::openChanged, this, [this](bool) { updateBusyState(); });
    connect(m_session, &DeviceSession::probeFinished, this, &MainWindow::onProbeFinished);
    connect(m_session, &DeviceSession::readFinished, this, &MainWindow::onReadFinished);
    connect(m_session, &DeviceSession::writeFinished, this, &MainWindow::onWriteFinished);
    connect(m_session, &DeviceSession::atFinished, this, &MainWindow::onAtFinished);
    connect(m_session, &DeviceSession::at3Finished, this, &MainWindow::onAt3Finished);
    connect(m_session, &DeviceSession::frameTraced, this, &MainWindow::onFrameTraced);
    connect(m_session, &DeviceSession::requestTraced, this, &MainWindow::onRequestTraced);

    ui->progress4g->setRange(0, 1); /* real range comes with the first progress() */
    buildFieldTable();
    onRefreshPorts();
    loadSettings();
    updateBusyState();
    note(tr("就绪：选择串口后点击“打开串口”。"));
}

MainWindow::~MainWindow()
{
    saveSettings();
    delete ui;
}

void MainWindow::note(const QString &text)
{
    statusBar()->showMessage(text, 10000);
}

void MainWindow::appendLog(const QString &text)
{
    ui->textLogView->appendPlainText(
        QStringLiteral("[%1] %2").arg(QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss.zzz")),
                                      text));
}

void MainWindow::appendAt(const QString &text)
{
    ui->textAtView->appendPlainText(
        QStringLiteral("[%1] %2").arg(QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss")), text));
}

void MainWindow::onFrameTraced(const QString &direction, const QByteArray &frame)
{
    const QString body = ui->checkHex->isChecked() ? hexText(frame) : toPrintable(frame);
    appendLog(QStringLiteral("%1 %2").arg(direction, body));
}

void MainWindow::onRequestTraced(const QString &json)
{
    appendLog(QStringLiteral("JSON > %1").arg(json));
}

void MainWindow::onRefreshPorts()
{
    const QString current = ui->comboPort->currentText();

    ui->comboPort->clear();
    ui->comboPort->addItems(DeviceSession::availablePorts());

    const int index = ui->comboPort->findText(current);
    if (index >= 0)
        ui->comboPort->setCurrentIndex(index);

    if (ui->comboPort->count() == 0)
        note(tr("没有检测到串口，请检查 USB 转串口设备后点击“刷新”。"));
    else
        note(tr("检测到 %1 个串口。").arg(ui->comboPort->count()));
}

void MainWindow::onToggleOpen()
{
    if (m_session->isOpen())
    {
        if (m_config->isRunning())
            m_config->abort();
        m_readbackRunning = false;
        m_session->close();
        note(tr("串口已关闭。"));
        updateBusyState();
        return;
    }

    const QString port = ui->comboPort->currentText();
    if (port.isEmpty())
    {
        QMessageBox::warning(this, tr("提示"), tr("没有可用串口，请检查连接后点击“刷新”。"));
        return;
    }

    const qint32 baud = ui->comboBaud->currentText().toInt();
    QString error;
    if (!m_session->open(port, baud, &error))
    {
        QMessageBox::critical(this, tr("打开串口失败"), error);
        return;
    }

    saveSettings();
    note(tr("已打开 %1 @ %2 8N1。").arg(port).arg(baud));
    updateBusyState();
}

void MainWindow::onProbe()
{
    m_session->probe();
    updateBusyState();
}

void MainWindow::onProbeFinished(bool ok)
{
    if (ok)
    {
        note(tr("链路探活成功：下位机应答正常。"));
        appendLog(QStringLiteral("probe ok"));
    }
    else
    {
        note(tr("链路探活失败：请检查串口、波特率与接线。"));
        appendLog(QStringLiteral("probe failed"));
    }
    updateBusyState();
}

void MainWindow::onReadConfig()
{
    clearHighlights();
    m_session->readConfig();
    updateBusyState();
}

void MainWindow::onReadFinished(bool ok, const QVariantMap &params, const QString &error)
{
    if (!ok)
    {
        note(tr("读取参数失败：%1").arg(error));
        appendLog(QStringLiteral("read failed: %1").arg(error));
        updateBusyState();
        return;
    }

    applyParams(params);
    m_snapshot = params;
    m_haveSnapshot = true;
    clearHighlights();
    note(tr("读取参数成功，共 %1 项。").arg(params.size()));
    appendLog(QStringLiteral("read ok (%1 fields)").arg(params.size()));
    updateBusyState();
}

void MainWindow::onWriteConfig()
{
    QVariantMap params;
    QString badKey;
    if (!collectParams(&params, &badKey))
    {
        highlightField(badKey);
        note(tr("字段 %1 不是合法的无符号整数，未下发。").arg(badKey));
        return;
    }

    /* only send what differs from the last known device values */
    QVariantMap changed;
    for (auto it = params.constBegin(); it != params.constEnd(); ++it)
    {
        const QString key = it.key();
        if (m_haveSnapshot && m_snapshot.value(key) == it.value())
            continue;
        changed.insert(key, it.value());
    }

    if (changed.isEmpty())
    {
        note(tr("没有字段发生变化，未下发。"));
        return;
    }

    clearHighlights();
    m_sent = changed;
    m_session->writeConfig(changed);
    updateBusyState();
}

void MainWindow::onWriteFinished(bool ok, const QString &error)
{
    const int count = m_sent.size();

    if (!ok)
    {
        m_sent.clear();
        highlightField(error);
        note(tr("写入参数失败：%1").arg(error));
        appendLog(QStringLiteral("write failed: %1").arg(error));
        updateBusyState();
        return;
    }

    for (auto it = m_sent.constBegin(); it != m_sent.constEnd(); ++it)
        m_snapshot.insert(it.key(), it.value());
    m_sent.clear();

    note(tr("写入参数成功（%1 项）。").arg(count));
    appendLog(QStringLiteral("write ok (%1 fields)").arg(count));
    updateBusyState();
}

void MainWindow::append4g(const QString &text)
{
    ui->text4gView->appendPlainText(
        QStringLiteral("[%1] %2").arg(QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss")), text));
}

void MainWindow::on4gPush()
{
    if (m_config->isRunning() || m_readbackRunning)
        return;

    QVariantMap params;
    params.insert(QStringLiteral("productKey"), ui->lineMqttProductKey->text().trimmed());
    params.insert(QStringLiteral("deviceKey"), ui->lineMqttDeviceKey->text().trimmed());
    params.insert(QStringLiteral("deviceSecret"), ui->lineMqttDeviceSecret->text().trimmed());
    params.insert(QStringLiteral("mqtt_addr"), ui->lineMqttAddr->text().trimmed());

    /* reconnect policy: optional. Empty = "leave the module's setting alone",
     * otherwise it must be 1..65535 (0 is rejected by the module). */
    ui->lineSockRstim->setStyleSheet(QString());
    ui->lineSockRsnum->setStyleSheet(QString());

    const QString rstimText = ui->lineSockRstim->text().trimmed();
    if (!rstimText.isEmpty())
    {
        bool parsed = false;
        const int rstim = rstimText.toInt(&parsed);
        if (!parsed || rstim < 1 || rstim > 65535)
        {
            ui->lineSockRstim->setStyleSheet(QStringLiteral("background:#ffe0e0;"));
            ui->lineSockRstim->setFocus();
            ui->lineSockRstim->selectAll();
            note(tr("重连间隔需为 1–65535 的整数（留空表示本次不下发）。"));
            return;
        }
        params.insert(QStringLiteral("sockrstim"), QString::number(rstim));
    }

    const QString rsnumText = ui->lineSockRsnum->text().trimmed();
    if (!rsnumText.isEmpty())
    {
        bool parsed = false;
        const int rsnum = rsnumText.toInt(&parsed);
        if (!parsed || rsnum < 1 || rsnum > 65535)
        {
            ui->lineSockRsnum->setStyleSheet(QStringLiteral("background:#ffe0e0;"));
            ui->lineSockRsnum->setFocus();
            ui->lineSockRsnum->selectAll();
            note(tr("重连次数需为 1–65535 的整数（0 会被模块拒绝，留空表示本次不下发）。"));
            return;
        }
        params.insert(QStringLiteral("sockrsnum"), QString::number(rsnum));
    }

    ui->progress4g->setValue(0);
    ui->label4gState->setText(tr("状态：下发中…"));
    append4g(tr("开始一键配置（逐条下发 AT 指令）"));

    m_atOwner = AtOwner::Config;
    m_config->start(params);
    updateBusyState();
}

void MainWindow::on4gStop()
{
    if (m_config->isRunning())
        m_config->abort();
}

void MainWindow::on4gProgress(int done, int total, const QString &command)
{
    ui->progress4g->setRange(0, total);
    ui->progress4g->setValue(done);
    ui->label4gState->setText(tr("状态：第 %1/%2 条 → %3").arg(done + 1).arg(total).arg(command));
}

void MainWindow::on4gFinished(bool ok, const QString &error)
{
    if (ok)
    {
        ui->progress4g->setValue(ui->progress4g->maximum());
        ui->label4gState->setText(tr("状态：完成（%1 条指令全部成功）").arg(m_config->commandTotal()));
        append4g(tr("配置完成：%1 条指令全部成功").arg(m_config->commandTotal()));
        append4g(tr("注意：AT+S 已保存并让模块重启，约 10 秒后再点「全部读取」复核。"));
        note(tr("4G 配置完成（模块保存重启中，约 10 秒后再复核）。"));
    }
    else
    {
        ui->label4gState->setText(tr("状态：失败（%1）").arg(error));
        append4g(tr("配置失败：%1").arg(error));
        note(tr("4G 配置失败：%1").arg(error));
    }
    updateBusyState();
}

void MainWindow::on4gLogLine(const QString &text)
{
    append4g(text);
}

void MainWindow::on4gRead()
{
    if (m_config->isRunning() || m_readbackRunning)
        return;

    m_readbackCommands = At4gConfigurator::readbackCommands();
    m_readbackValues.clear();
    m_readbackIndex = 0;
    m_readbackRunning = true;
    append4g(tr("读取模块当前 MQTT 参数…"));
    sendNextReadbackCommand();
    updateBusyState();
}

void MainWindow::sendNextReadbackCommand()
{
    if (m_readbackIndex >= m_readbackCommands.size())
    {
        finishReadback();
        return;
    }

    m_atOwner = AtOwner::Readback;
    m_session->sendAt(m_readbackCommands.at(m_readbackIndex));
}

void MainWindow::handleReadbackReply(bool ok, const QString &data, const QString &error)
{
    static const char *const tags[] = { "MQTTSVR", "MQTTUSER", "MQTTPSW", "MQTTCID",
                                        "SOCKRSTIM", "SOCKRSNUM" };
    const QString tag = QString::fromLatin1(tags[m_readbackIndex]);
    const QString value = ok ? At4gConfigurator::parseReplyValue(data, tag) : QString();

    if (!value.isEmpty())
    {
        m_readbackValues.insert(tag, value);
        append4g(QStringLiteral("%1 = %2").arg(tag, value));
    }
    else
    {
        const QString detail = ok ? data.trimmed() : error;
        append4g(QStringLiteral("%1 未取到：%2")
                     .arg(tag, detail.isEmpty() ? tr("(无回显)") : detail));
    }

    ++m_readbackIndex;
    sendNextReadbackCommand();
}

void MainWindow::finishReadback()
{
    m_readbackRunning = false;

    if (m_readbackValues.contains(QStringLiteral("MQTTSVR")))
        ui->lineMqttAddr->setText(m_readbackValues.value(QStringLiteral("MQTTSVR")).toString());
    if (m_readbackValues.contains(QStringLiteral("MQTTPSW")))
        ui->lineMqttDeviceSecret->setText(m_readbackValues.value(QStringLiteral("MQTTPSW")).toString());
    if (m_readbackValues.contains(QStringLiteral("MQTTCID")))
        ui->lineMqttDeviceKey->setText(m_readbackValues.value(QStringLiteral("MQTTCID")).toString());
    else if (m_readbackValues.contains(QStringLiteral("MQTTUSER")))
        ui->lineMqttDeviceKey->setText(m_readbackValues.value(QStringLiteral("MQTTUSER")).toString());

    /* the two reconnect settings are read back too; the module answers
     * "+SOCKRSTIM:30" / "+SOCKRSNUM:65535" */
    if (m_readbackValues.contains(QStringLiteral("SOCKRSTIM")))
        ui->lineSockRstim->setText(m_readbackValues.value(QStringLiteral("SOCKRSTIM")).toString());
    if (m_readbackValues.contains(QStringLiteral("SOCKRSNUM")))
        ui->lineSockRsnum->setText(m_readbackValues.value(QStringLiteral("SOCKRSNUM")).toString());

    append4g(tr("读取结束（productKey 模块内没有对应项，保持本机输入）"));
    note(tr("4G 参数读取结束。"));
    saveSettings();
    updateBusyState();
}

void MainWindow::onSendAt()
{
    const QString command = ui->lineAtCmd->text().trimmed();
    if (command.isEmpty())
        return;

    if (m_config->isRunning() || m_readbackRunning)
    {
        note(tr("4G 配置/读取正在执行，AT 控制台暂时不可用。"));
        return;
    }

    appendAt(tr("→ %1").arg(command));
    m_atOwner = AtOwner::Console;
    m_session->sendAt(command);
    updateBusyState();
}

void MainWindow::onAtFinished(bool ok, const QString &data, const QString &error)
{
    /* the AT channel is shared: route the reply to whoever issued the command */
    const AtOwner owner = m_atOwner;
    m_atOwner = AtOwner::None;

    if (owner == AtOwner::Config)
        return; /* the configurator logs its own progress */

    if (owner == AtOwner::Readback)
    {
        handleReadbackReply(ok, data, error);
        return;
    }

    if (!ok)
    {
        appendAt(tr("← 失败：%1").arg(error));
        note(tr("AT 指令失败：%1").arg(error));
    }
    else
    {
        appendAt(tr("← %1").arg(data.isEmpty() ? tr("(无回显)") : data));
        note(tr("AT 指令完成。"));
    }
    updateBusyState();
}

void MainWindow::onClearLog()
{
    ui->textLogView->clear();
    note(tr("日志已清空。"));
}

void MainWindow::onClearAtLog()
{
    ui->textAtView->clear();
    note(tr("AT 控制台日志已清空。"));
}

void MainWindow::onClear4gLog()
{
    ui->text4gView->clear();
    note(tr("4G 配置日志已清空。"));
}

/* ------------------------------------------------------------------ 蓝牙 */

void MainWindow::appendBle(const QString &text)
{
    ui->textBleView->appendPlainText(
        QStringLiteral("[%1] %2").arg(QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss")), text));
}

void MainWindow::startBleQuery(BleQuery query, const QString &command)
{
    if (!m_session->isOpen())
    {
        note(tr("请先打开串口。"));
        return;
    }
    if (m_session->isBusy())
        return;

    m_bleQuery = query;
    appendBle(tr("→ %1").arg(command));
    m_session->sendAt3(command);
    updateBusyState();
}

void MainWindow::onBleReadAddr()
{
    startBleQuery(BleQuery::Addr, QStringLiteral("AT+LADDR"));
}

void MainWindow::onBleReadName()
{
    startBleQuery(BleQuery::Name, QStringLiteral("AT+NAME"));
}

void MainWindow::onBleReadVersion()
{
    startBleQuery(BleQuery::Version, QStringLiteral("AT+VERSION"));
}

void MainWindow::onBleSend()
{
    const QString command = ui->lineBleCmd->text().trimmed();
    if (command.isEmpty())
        return;

    startBleQuery(BleQuery::Manual, command);
}

void MainWindow::onBleClearLog()
{
    ui->textBleView->clear();
    note(tr("蓝牙日志已清空。"));
}

void MainWindow::onAt3Finished(bool ok, const QString &data, const QString &error)
{
    const BleQuery query = m_bleQuery;
    m_bleQuery = BleQuery::None;

    if (!ok)
    {
        appendBle(tr("← 失败：%1").arg(error));
        note(tr("蓝牙模块无响应：可能手机正连着（AT 只在未连接时生效）、模块未装或 PD8/PD9 接线不对。"));
        updateBusyState();
        return;
    }

    appendBle(tr("← %1").arg(data.isEmpty() ? tr("(无回显)") : data));

    switch (query)
    {
    case BleQuery::Addr:
    {
        const QString mac = bleMacOf(data);
        if (mac.isEmpty())
        {
            note(tr("未从回显里解析到 MAC 地址，请看日志里的原始回显。"));
        }
        else
        {
            ui->lineBleAddr->setText(mac);
            note(tr("蓝牙地址：%1").arg(mac));
        }
        break;
    }
    case BleQuery::Name:
        ui->lineBleName->setText(bleReplyValue(data, QStringLiteral("NAME")));
        break;
    case BleQuery::Version:
        ui->lineBleVersion->setText(bleReplyValue(data, QStringLiteral("VERSION")));
        break;
    default:
        break;
    }

    updateBusyState();
}

void MainWindow::onExportLog()
{
    const QString path = QFileDialog::getSaveFileName(
        this, tr("导出日志"), QStringLiteral("sbf20_log.txt"), tr("文本文件 (*.txt)"));
    if (path.isEmpty())
        return;

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
    {
        QMessageBox::warning(this, tr("导出失败"), file.errorString());
        return;
    }

    QTextStream stream(&file);
    stream << ui->textLogView->toPlainText();
    file.close();

    note(tr("日志已导出：%1").arg(path));
}

void MainWindow::buildFieldTable()
{
    /* key                          widget                          numeric */
    m_fields = {
        { "motor_warning_curr_upper", ui->lineMotorWarningCurrUpper,  true  },
        { "gate_maxheight",           ui->lineGateMaxheight,          true  },
        { "gate_maxturns",            ui->lineGateMaxturns,           true  },
        { "gate_kaidu",               ui->lineGateKaidu,              true  },
        { "gate_kaidu_count",         ui->lineGateKaiduCount,         true  },
        { "OTA_flag",                 ui->lineOtaFlag,                true  },
        { "OTA_ver",                  ui->lineOtaVer,                 false },
        { "OTA_size",                 ui->lineOtaSize,                true  },
        { "UPDATE_flag",              ui->lineUpdateFlag,             true  },
        { "productKey",               ui->lineMqttProductKey,         false },
        { "deviceKey",                ui->lineMqttDeviceKey,          false },
        { "deviceSecret",             ui->lineMqttDeviceSecret,       false },
        { "mqtt_addr",                ui->lineMqttAddr,               false },
    };
}

bool MainWindow::collectParams(QVariantMap *out, QString *badKey)
{
    out->clear();

    for (const ParamField &field : m_fields)
    {
        const QString key = QString::fromLatin1(field.key);
        if (key == QLatin1String("OTA_size"))
            continue; /* read only */

        const QString text = field.edit->text().trimmed();

        if (field.numeric)
        {
            bool ok = false;
            const qulonglong value = text.toULongLong(&ok, 10);
            if (text.isEmpty() || !ok)
            {
                *badKey = key;
                return false;
            }
            out->insert(key, value);
        }
        else
        {
            out->insert(key, text);
        }
    }
    return true;
}

void MainWindow::applyParams(const QVariantMap &params)
{
    for (const ParamField &field : m_fields)
    {
        const QString key = QString::fromLatin1(field.key);
        if (!params.contains(key))
            continue;

        const QVariant value = params.value(key);
        if (field.numeric && value.typeId() != QMetaType::QString)
            field.edit->setText(QString::number(value.toULongLong()));
        else
            field.edit->setText(value.toString());
    }

    updateOtaFlagHex();
}

void MainWindow::clearHighlights()
{
    for (const ParamField &field : m_fields)
        field.edit->setStyleSheet(QString());
}

void MainWindow::highlightField(const QString &key)
{
    if (key.isEmpty())
        return;

    for (const ParamField &field : m_fields)
    {
        if (QString::fromLatin1(field.key) == key)
        {
            field.edit->setStyleSheet(QStringLiteral("background:#ffe0e0;"));
            field.edit->setFocus();
            field.edit->selectAll();
            return;
        }
    }
}

void MainWindow::updateOtaFlagHex()
{
    bool ok = false;
    const qulonglong value = ui->lineOtaFlag->text().trimmed().toULongLong(&ok, 10);

    if (!ok)
    {
        ui->labelOtaFlagHex->setText(QStringLiteral("= -"));
        return;
    }

    const QString hex = QString::number(value, 16).toUpper();
    ui->labelOtaFlagHex->setText(QStringLiteral("= 0x%1").arg(hex, 8, QLatin1Char('0')));
}

void MainWindow::updateBusyState()
{
    const bool open = m_session->isOpen();
    const bool busy = m_session->isBusy();
    const bool ready = open && !busy;

    ui->btnOpen->setText(open ? tr("关闭串口") : tr("打开串口"));
    ui->comboPort->setEnabled(!open);
    ui->comboBaud->setEnabled(!open);
    ui->btnRefresh->setEnabled(!open);

    ui->btnProbe->setEnabled(ready);
    ui->btnRead->setEnabled(ready);
    ui->btnWrite->setEnabled(ready);

    const bool cfgRunning = m_config->isRunning();
    const bool g4Busy = cfgRunning || m_readbackRunning;

    ui->btn4gPush->setEnabled(ready && !g4Busy);
    ui->btn4gRead->setEnabled(ready && !g4Busy);
    ui->btn4gStop->setEnabled(cfgRunning);

    ui->btnAtSend->setEnabled(ready && !g4Busy);
    ui->btnAtCsq->setEnabled(ready && !g4Busy);
    ui->btnAtIccid->setEnabled(ready && !g4Busy);
    ui->btnAtCclk->setEnabled(ready && !g4Busy);
    ui->btnAtMqttCid->setEnabled(ready && !g4Busy);
    ui->lineAtCmd->setEnabled(open && !g4Busy);

    ui->btnBleAddr->setEnabled(ready);
    ui->btnBleName->setEnabled(ready);
    ui->btnBleVersion->setEnabled(ready);
    ui->btnBleSend->setEnabled(ready);
    ui->lineBleCmd->setEnabled(open);

    if (open)
        ui->labelState->setText(tr("已连接 %1 @ %2").arg(m_session->portName()).arg(m_session->baudRate()));
    else
        ui->labelState->setText(tr("未连接"));
}

void MainWindow::loadSettings()
{
    QSettings settings(QStringLiteral("qibiji"), QStringLiteral("qibiji_init"));

    const QString port = settings.value(QStringLiteral("serial/port")).toString();
    const QString baud = settings.value(QStringLiteral("serial/baud"), QStringLiteral("115200")).toString();

    const int baudIndex = ui->comboBaud->findText(baud);
    if (baudIndex >= 0)
        ui->comboBaud->setCurrentIndex(baudIndex);

    const int portIndex = ui->comboPort->findText(port);
    if (portIndex >= 0)
        ui->comboPort->setCurrentIndex(portIndex);

    for (const ParamField &field : m_fields)
    {
        const QString key = QString::fromLatin1(field.key);
        if (key == QLatin1String("OTA_size"))
            continue; /* read only, always comes from the device */
        field.edit->setText(settings.value(QStringLiteral("param/") + key).toString());
    }

    /* the 4G parameters live on the PC side only (same settings keys as before) */
    ui->lineMqttProductKey->setText(settings.value(QStringLiteral("param/productKey")).toString());
    ui->lineMqttDeviceKey->setText(settings.value(QStringLiteral("param/deviceKey")).toString());
    ui->lineMqttDeviceSecret->setText(settings.value(QStringLiteral("param/deviceSecret")).toString());
    ui->lineMqttAddr->setText(settings.value(QStringLiteral("param/mqtt_addr")).toString());
    /* the two reconnect fields always start empty (optional, not remembered) */
    ui->lineSockRstim->clear();
    ui->lineSockRsnum->clear();

    updateOtaFlagHex();
}

void MainWindow::saveSettings()
{
    QSettings settings(QStringLiteral("qibiji"), QStringLiteral("qibiji_init"));

    settings.setValue(QStringLiteral("serial/port"), ui->comboPort->currentText());
    settings.setValue(QStringLiteral("serial/baud"), ui->comboBaud->currentText());
    for (const ParamField &field : m_fields)
    {
        const QString key = QString::fromLatin1(field.key);
        if (key == QLatin1String("OTA_size"))
            continue;
        settings.setValue(QStringLiteral("param/") + key, field.edit->text());
    }

    settings.setValue(QStringLiteral("param/productKey"), ui->lineMqttProductKey->text());
    settings.setValue(QStringLiteral("param/deviceKey"), ui->lineMqttDeviceKey->text());
    settings.setValue(QStringLiteral("param/deviceSecret"), ui->lineMqttDeviceSecret->text());
    settings.setValue(QStringLiteral("param/mqtt_addr"), ui->lineMqttAddr->text());
}
