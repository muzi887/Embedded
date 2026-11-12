#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QVector>
#include <QVariantMap>

QT_BEGIN_NAMESPACE
namespace Ui {
class MainWindow;
}
QT_END_NAMESPACE

class DeviceSession;
class At4gConfigurator;
class QLineEdit;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

private slots:
    void onRefreshPorts();
    void onToggleOpen();
    void onProbe();
    void onReadConfig();
    void onWriteConfig();
    void onSendAt();
    void onClearLog();
    void onClearAtLog();
    void onClear4gLog();
    void onExportLog();

    void on4gPush();
    void on4gRead();
    void on4gStop();
    void on4gProgress(int done, int total, const QString &command);
    void on4gFinished(bool ok, const QString &error);
    void on4gLogLine(const QString &text);

    void onBleReadAddr();
    void onBleReadName();
    void onBleReadVersion();
    void onBleSend();
    void onBleClearLog();
    void onAt3Finished(bool ok, const QString &data, const QString &error);

    void onProbeFinished(bool ok);
    void onReadFinished(bool ok, const QVariantMap &params, const QString &error);
    void onWriteFinished(bool ok, const QString &error);
    void onAtFinished(bool ok, const QString &data, const QString &error);
    void onFrameTraced(const QString &direction, const QByteArray &frame);
    void onRequestTraced(const QString &json);

private:
    /* who currently owns the "at" command channel */
    enum class AtOwner { None, Console, Config, Readback };

    /* which BLE query is currently in flight (routes the reply to a field) */
    enum class BleQuery { None, Addr, Name, Version, Manual };

    struct ParamField
    {
        const char *key;
        QLineEdit *edit;
        bool numeric;
    };

    void buildFieldTable();
    void loadSettings();
    void saveSettings();
    void appendLog(const QString &text);
    void appendAt(const QString &text);
    void append4g(const QString &text);
    void appendBle(const QString &text);
    void note(const QString &text);
    void updateBusyState();
    bool collectParams(QVariantMap *out, QString *badKey);
    void applyParams(const QVariantMap &params);
    void clearHighlights();
    void highlightField(const QString &key);
    void updateOtaFlagHex();
    void sendNextReadbackCommand();
    void handleReadbackReply(bool ok, const QString &data, const QString &error);
    void finishReadback();
    void startBleQuery(BleQuery query, const QString &command);

    Ui::MainWindow *ui;
    DeviceSession *m_session = nullptr;
    At4gConfigurator *m_config = nullptr;

    QVector<ParamField> m_fields;
    QVariantMap m_snapshot;   /* last known device values */
    QVariantMap m_sent;       /* values of the in-flight write */
    bool m_haveSnapshot = false;
    AtOwner m_atOwner = AtOwner::None;

    QStringList m_readbackCommands;
    QVariantMap m_readbackValues;
    int m_readbackIndex = 0;
    bool m_readbackRunning = false;
    BleQuery m_bleQuery = BleQuery::None;
};

#endif // MAINWINDOW_H
