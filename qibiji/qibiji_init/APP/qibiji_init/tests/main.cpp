/*
 * Headless smoke test for the Qt upper computer protocol stack.
 *
 * Drives the very same DeviceSession class the GUI uses against a real lower
 * board:
 *   probe -> read all -> write test values -> read back -> restore -> read back
 *   -> read the 4G module's MQTT parameters with "?" queries (read only)
 *
 * The round trip covers the gate words, the OTA block and the 4 MQTT strings
 * (productKey / deviceKey / deviceSecret / mqtt_addr at 64/96/128/160).
 *
 * The original EEPROM content is always written back, so the test is safe on a
 * device that is already configured. The 4G part is read only and never sends
 * the 22-command configuration.
 *
 * Usage: sessiontest.exe [COM10]
 */
#include <QCoreApplication>
#include <QStringList>
#include <QTextStream>
#include <QVariantMap>

#include "at4gconfigurator.h"
#include "devicesession.h"

namespace {

const char *const kGateKeys[] = {
    "motor_warning_curr_upper",
    "gate_maxheight",
    "gate_maxturns",
    "gate_kaidu",
    "gate_kaidu_count"
};

/* EEPROM MQTT slots (64/96/128/160, 32 chars each) */
const char *const kMqttKeys[] = {
    "productKey", "deviceKey", "deviceSecret", "mqtt_addr"
};

const char *const kMqttTags[] = { "MQTTSVR", "MQTTUSER", "MQTTPSW", "MQTTCID",
                                  "SOCKRSTIM", "SOCKRSNUM" };

} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);

    QTextStream out(stdout);
    const QString portName = (argc > 1) ? QString::fromLocal8Bit(argv[1])
                                        : QStringLiteral("COM10");

    DeviceSession session;

    enum Phase { ReadOriginal, WriteTest, ReadTest, WriteRestore, ReadRestore, MqttReadback, Done };
    Phase phase = ReadOriginal;
    QVariantMap original;
    QVariantMap testValues;
    QVariantMap restoreValues;
    QStringList mqttCommands;
    QVariantMap mqttValues;
    int mqttIndex = 0;
    int failures = 0;

    auto say = [&](const QString &text) {
        out << text << Qt::endl;
        out.flush();
    };
    auto fail = [&](const QString &text) {
        ++failures;
        say(QStringLiteral("!! ") + text);
    };
    auto finish = [&]() {
        say(failures == 0 ? QStringLiteral("== EEPROM PARAM TEST OK ==")
                          : QStringLiteral("== EEPROM PARAM TEST FAILED (%1) ==").arg(failures));
        app.exit(failures == 0 ? 0 : 1);
    };

    auto compare = [&](const QVariantMap &actual, const QVariantMap &expected,
                       const QString &tag) -> int {
        int diffs = 0;
        for (auto it = expected.constBegin(); it != expected.constEnd(); ++it)
        {
            const QVariant value = actual.value(it.key());
            if (value != it.value())
            {
                ++diffs;
                say(QStringLiteral("   %1: %2 -> read %3 (expected %4)")
                        .arg(it.key(), tag, value.toString(), it.value().toString()));
            }
        }
        return diffs;
    };

    QObject::connect(&session, &DeviceSession::probeFinished, [&](bool ok) {
        if (ok)
        {
            say(QStringLiteral("probe        : OK"));
            session.readConfig();
        }
        else
        {
            fail(QStringLiteral("probe        : FAIL (no answer)"));
            finish();
        }
    });

    QObject::connect(&session, &DeviceSession::readFinished,
                     [&](bool ok, const QVariantMap &params, const QString &error) {
        if (!ok)
        {
            fail(QStringLiteral("read         : FAIL %1").arg(error));
            if (phase == ReadTest && !restoreValues.isEmpty())
            {
                phase = WriteRestore;
                session.writeConfig(restoreValues);
                return;
            }
            finish();
            return;
        }

        if (phase == ReadOriginal)
        {
            original = params;
            say(QStringLiteral("read all     : OK (%1 fields)").arg(params.size()));

            testValues = params;
            for (const char *key : kGateKeys)
            {
                const QString k = QString::fromLatin1(key);
                testValues.insert(k, original.value(k).toULongLong() + 1);
            }
            testValues.insert(QStringLiteral("OTA_ver"), QStringLiteral("v9.9.9-test"));

            /* MQTT strings: overwrite with short markers, restore afterwards */
            testValues.insert(QStringLiteral("productKey"), QStringLiteral("test-pk"));
            testValues.insert(QStringLiteral("deviceKey"), QStringLiteral("test-dk"));
            testValues.insert(QStringLiteral("deviceSecret"), QStringLiteral("test-secret"));
            testValues.insert(QStringLiteral("mqtt_addr"), QStringLiteral("1.2.3.4:1883"));

            restoreValues.clear();
            for (const char *key : kGateKeys)
            {
                const QString k = QString::fromLatin1(key);
                if (original.contains(k))
                    restoreValues.insert(k, original.value(k));
            }
            if (original.contains(QStringLiteral("OTA_ver")))
                restoreValues.insert(QStringLiteral("OTA_ver"), original.value(QStringLiteral("OTA_ver")));
            for (const char *key : kMqttKeys)
            {
                const QString k = QString::fromLatin1(key);
                if (original.contains(k))
                    restoreValues.insert(k, original.value(k));
            }

            phase = WriteTest;
            session.writeConfig(testValues);
            return;
        }

        if (phase == ReadTest)
        {
            const int diffs = compare(params, testValues, QStringLiteral("test"));
            if (diffs == 0)
                say(QStringLiteral("verify write : OK (changed values read back)"));
            else
                fail(QStringLiteral("verify write : FAIL (%1 fields differ)").arg(diffs));

            phase = WriteRestore;
            session.writeConfig(restoreValues);
            return;
        }

        if (phase == ReadRestore)
        {
            const int diffs = compare(params, original, QStringLiteral("restore"));
            if (diffs == 0)
                say(QStringLiteral("verify restor: OK (original values read back)"));
            else
                fail(QStringLiteral("verify restor: FAIL (%1 fields differ)").arg(diffs));

            /* 4G read back: read only, never reconfigures the module */
            say(QStringLiteral("4G readback  : querying the module (read only)"));
            mqttCommands = At4gConfigurator::readbackCommands();
            mqttValues.clear();
            mqttIndex = 0;
            phase = MqttReadback;
            session.sendAt(mqttCommands.at(mqttIndex));
            return;
        }
    });

    QObject::connect(&session, &DeviceSession::writeFinished,
                     [&](bool ok, const QString &error) {
        if (!ok)
        {
            fail(QStringLiteral("write        : FAIL at phase %1: %2")
                     .arg(static_cast<int>(phase))
                     .arg(error));

            if (phase == WriteTest && !restoreValues.isEmpty())
            {
                phase = WriteRestore;
                session.writeConfig(restoreValues);
                return;
            }
            finish();
            return;
        }

        if (phase == WriteTest)
        {
            say(QStringLiteral("write test   : OK"));
            phase = ReadTest;
            session.readConfig();
        }
        else if (phase == WriteRestore)
        {
            say(QStringLiteral("restore      : OK"));
            phase = ReadRestore;
            session.readConfig();
        }
    });

    QObject::connect(&session, &DeviceSession::atFinished,
                     [&](bool ok, const QString &data, const QString &error) {
        if (phase != MqttReadback)
            return;

        const QString tag = QString::fromLatin1(kMqttTags[mqttIndex]);
        const QString value = ok ? At4gConfigurator::parseReplyValue(data, tag) : QString();

        if (value.isEmpty())
        {
            const QString detail = ok ? data.trimmed() : error;
            say(QStringLiteral("   %1 : no value (%2)")
                    .arg(tag, detail.isEmpty() ? QStringLiteral("empty reply") : detail));
        }
        else
        {
            mqttValues.insert(tag, value);
            say(QStringLiteral("   %1 = %2").arg(tag, value));
        }

        ++mqttIndex;
        if (mqttIndex < mqttCommands.size())
        {
            session.sendAt(mqttCommands.at(mqttIndex));
            return;
        }

        if (mqttValues.isEmpty())
            say(QStringLiteral("4G readback  : no value returned (module absent or query unsupported)"));
        else
            say(QStringLiteral("4G readback  : OK (%1 values)").arg(mqttValues.size()));

        phase = Done;
        finish();
    });

    QString error;
    if (!session.open(portName, 115200, &error))
    {
        say(QStringLiteral("open %1 failed: %2").arg(portName, error));
        return 2;
    }

    say(QStringLiteral("eeprom parameter test on %1 @115200").arg(portName));
    session.probe();
    return app.exec();
}
