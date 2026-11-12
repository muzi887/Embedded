/********************************************************************************
** Form generated from reading UI file 'mainwindow.ui'
**
** Created by: Qt User Interface Compiler version 6.11.2
**
** WARNING! All changes made in this file will be lost when recompiling UI file!
********************************************************************************/

#ifndef UI_MAINWINDOW_H
#define UI_MAINWINDOW_H

#include <QtCore/QVariant>
#include <QtWidgets/QApplication>
#include <QtWidgets/QCheckBox>
#include <QtWidgets/QComboBox>
#include <QtWidgets/QFormLayout>
#include <QtWidgets/QGroupBox>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QLabel>
#include <QtWidgets/QLineEdit>
#include <QtWidgets/QMainWindow>
#include <QtWidgets/QPlainTextEdit>
#include <QtWidgets/QProgressBar>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QSpacerItem>
#include <QtWidgets/QStatusBar>
#include <QtWidgets/QTabWidget>
#include <QtWidgets/QVBoxLayout>
#include <QtWidgets/QWidget>

QT_BEGIN_NAMESPACE

class Ui_MainWindow
{
public:
    QWidget *centralwidget;
    QVBoxLayout *centralLayout;
    QTabWidget *tabWidget;
    QWidget *tabSerial;
    QVBoxLayout *serialLayout;
    QHBoxLayout *serialRow1;
    QLabel *labelPortName;
    QComboBox *comboPort;
    QPushButton *btnRefresh;
    QLabel *labelBaud;
    QComboBox *comboBaud;
    QPushButton *btnOpen;
    QSpacerItem *serialSpacer;
    QHBoxLayout *serialRow2;
    QPushButton *btnProbe;
    QLabel *labelState;
    QLabel *labelSerialHint;
    QSpacerItem *serialSpacer2;
    QWidget *tabParam;
    QVBoxLayout *paramLayout;
    QGroupBox *groupGate;
    QFormLayout *gateForm;
    QLabel *labelMotorWarningCurrUpper;
    QLineEdit *lineMotorWarningCurrUpper;
    QLabel *labelGateMaxheight;
    QLineEdit *lineGateMaxheight;
    QLabel *labelGateMaxturns;
    QLineEdit *lineGateMaxturns;
    QLabel *labelGateKaidu;
    QLineEdit *lineGateKaidu;
    QLabel *labelGateKaiduCount;
    QLineEdit *lineGateKaiduCount;
    QGroupBox *groupOta;
    QFormLayout *otaForm;
    QLabel *labelOtaFlag;
    QHBoxLayout *otaFlagRow;
    QLineEdit *lineOtaFlag;
    QLabel *labelOtaFlagHex;
    QLabel *labelOtaVer;
    QLineEdit *lineOtaVer;
    QLabel *labelOtaSize;
    QLineEdit *lineOtaSize;
    QLabel *labelUpdateFlag;
    QLineEdit *lineUpdateFlag;
    QGroupBox *groupMqtt;
    QFormLayout *mqttForm;
    QLabel *labelMqttProductKey;
    QLineEdit *lineMqttProductKey;
    QLabel *labelMqttDeviceKey;
    QLineEdit *lineMqttDeviceKey;
    QLabel *labelMqttDeviceSecret;
    QLineEdit *lineMqttDeviceSecret;
    QLabel *labelMqttAddr;
    QLineEdit *lineMqttAddr;
    QLabel *labelParamHint;
    QHBoxLayout *paramRow1;
    QPushButton *btnRead;
    QPushButton *btnWrite;
    QSpacerItem *paramSpacer;
    QSpacerItem *paramSpacer2;
    QWidget *tab4g;
    QVBoxLayout *g4Layout;
    QGroupBox *group4g;
    QFormLayout *g4Form;
    QLabel *labelSockRstim;
    QLineEdit *lineSockRstim;
    QLabel *labelSockRsnum;
    QLineEdit *lineSockRsnum;
    QLabel *label4gHint;
    QHBoxLayout *g4Row1;
    QPushButton *btn4gPush;
    QPushButton *btn4gRead;
    QPushButton *btn4gStop;
    QPushButton *btnClear4g;
    QSpacerItem *g4Spacer;
    QHBoxLayout *g4Row2;
    QLabel *labelProgress;
    QProgressBar *progress4g;
    QLabel *label4gState;
    QPlainTextEdit *text4gView;
    QWidget *tabBle;
    QVBoxLayout *bleLayout;
    QGroupBox *groupBle;
    QFormLayout *bleForm;
    QLabel *labelBleAddr;
    QLineEdit *lineBleAddr;
    QLabel *labelBleName;
    QLineEdit *lineBleName;
    QLabel *labelBleVersion;
    QLineEdit *lineBleVersion;
    QLabel *labelBleHint;
    QHBoxLayout *bleRow1;
    QPushButton *btnBleAddr;
    QPushButton *btnBleName;
    QPushButton *btnBleVersion;
    QPushButton *btnBleClearLog;
    QSpacerItem *bleSpacer1;
    QHBoxLayout *bleRow2;
    QLabel *labelBleCmd;
    QLineEdit *lineBleCmd;
    QPushButton *btnBleSend;
    QPlainTextEdit *textBleView;
    QWidget *tabAt;
    QVBoxLayout *atLayout;
    QHBoxLayout *atRow1;
    QLabel *labelAtCmd;
    QLineEdit *lineAtCmd;
    QPushButton *btnAtSend;
    QHBoxLayout *atRow2;
    QPushButton *btnAtCsq;
    QPushButton *btnAtIccid;
    QPushButton *btnAtCclk;
    QPushButton *btnAtMqttCid;
    QPushButton *btnClearAt;
    QSpacerItem *atSpacer;
    QPlainTextEdit *textAtView;
    QWidget *tabLog;
    QVBoxLayout *logLayout;
    QHBoxLayout *logRow1;
    QCheckBox *checkHex;
    QPushButton *btnClearLog;
    QPushButton *btnExportLog;
    QSpacerItem *logSpacer;
    QPlainTextEdit *textLogView;
    QStatusBar *statusbar;

    void setupUi(QMainWindow *MainWindow)
    {
        if (MainWindow->objectName().isEmpty())
            MainWindow->setObjectName("MainWindow");
        MainWindow->resize(880, 680);
        centralwidget = new QWidget(MainWindow);
        centralwidget->setObjectName("centralwidget");
        centralLayout = new QVBoxLayout(centralwidget);
        centralLayout->setObjectName("centralLayout");
        tabWidget = new QTabWidget(centralwidget);
        tabWidget->setObjectName("tabWidget");
        tabSerial = new QWidget();
        tabSerial->setObjectName("tabSerial");
        serialLayout = new QVBoxLayout(tabSerial);
        serialLayout->setObjectName("serialLayout");
        serialRow1 = new QHBoxLayout();
        serialRow1->setObjectName("serialRow1");
        labelPortName = new QLabel(tabSerial);
        labelPortName->setObjectName("labelPortName");

        serialRow1->addWidget(labelPortName);

        comboPort = new QComboBox(tabSerial);
        comboPort->setObjectName("comboPort");
        comboPort->setMinimumWidth(160);

        serialRow1->addWidget(comboPort);

        btnRefresh = new QPushButton(tabSerial);
        btnRefresh->setObjectName("btnRefresh");

        serialRow1->addWidget(btnRefresh);

        labelBaud = new QLabel(tabSerial);
        labelBaud->setObjectName("labelBaud");

        serialRow1->addWidget(labelBaud);

        comboBaud = new QComboBox(tabSerial);
        comboBaud->addItem(QString());
        comboBaud->addItem(QString());
        comboBaud->addItem(QString());
        comboBaud->setObjectName("comboBaud");

        serialRow1->addWidget(comboBaud);

        btnOpen = new QPushButton(tabSerial);
        btnOpen->setObjectName("btnOpen");

        serialRow1->addWidget(btnOpen);

        serialSpacer = new QSpacerItem(40, 20, QSizePolicy::Policy::Expanding, QSizePolicy::Policy::Minimum);

        serialRow1->addItem(serialSpacer);


        serialLayout->addLayout(serialRow1);

        serialRow2 = new QHBoxLayout();
        serialRow2->setObjectName("serialRow2");
        btnProbe = new QPushButton(tabSerial);
        btnProbe->setObjectName("btnProbe");

        serialRow2->addWidget(btnProbe);

        labelState = new QLabel(tabSerial);
        labelState->setObjectName("labelState");

        serialRow2->addWidget(labelState);


        serialLayout->addLayout(serialRow2);

        labelSerialHint = new QLabel(tabSerial);
        labelSerialHint->setObjectName("labelSerialHint");
        labelSerialHint->setWordWrap(true);

        serialLayout->addWidget(labelSerialHint);

        serialSpacer2 = new QSpacerItem(20, 40, QSizePolicy::Policy::Minimum, QSizePolicy::Policy::Expanding);

        serialLayout->addItem(serialSpacer2);

        tabWidget->addTab(tabSerial, QString());
        tabParam = new QWidget();
        tabParam->setObjectName("tabParam");
        paramLayout = new QVBoxLayout(tabParam);
        paramLayout->setObjectName("paramLayout");
        groupGate = new QGroupBox(tabParam);
        groupGate->setObjectName("groupGate");
        gateForm = new QFormLayout(groupGate);
        gateForm->setObjectName("gateForm");
        labelMotorWarningCurrUpper = new QLabel(groupGate);
        labelMotorWarningCurrUpper->setObjectName("labelMotorWarningCurrUpper");

        gateForm->setWidget(0, QFormLayout::ItemRole::LabelRole, labelMotorWarningCurrUpper);

        lineMotorWarningCurrUpper = new QLineEdit(groupGate);
        lineMotorWarningCurrUpper->setObjectName("lineMotorWarningCurrUpper");

        gateForm->setWidget(0, QFormLayout::ItemRole::FieldRole, lineMotorWarningCurrUpper);

        labelGateMaxheight = new QLabel(groupGate);
        labelGateMaxheight->setObjectName("labelGateMaxheight");

        gateForm->setWidget(1, QFormLayout::ItemRole::LabelRole, labelGateMaxheight);

        lineGateMaxheight = new QLineEdit(groupGate);
        lineGateMaxheight->setObjectName("lineGateMaxheight");

        gateForm->setWidget(1, QFormLayout::ItemRole::FieldRole, lineGateMaxheight);

        labelGateMaxturns = new QLabel(groupGate);
        labelGateMaxturns->setObjectName("labelGateMaxturns");

        gateForm->setWidget(2, QFormLayout::ItemRole::LabelRole, labelGateMaxturns);

        lineGateMaxturns = new QLineEdit(groupGate);
        lineGateMaxturns->setObjectName("lineGateMaxturns");

        gateForm->setWidget(2, QFormLayout::ItemRole::FieldRole, lineGateMaxturns);

        labelGateKaidu = new QLabel(groupGate);
        labelGateKaidu->setObjectName("labelGateKaidu");

        gateForm->setWidget(3, QFormLayout::ItemRole::LabelRole, labelGateKaidu);

        lineGateKaidu = new QLineEdit(groupGate);
        lineGateKaidu->setObjectName("lineGateKaidu");

        gateForm->setWidget(3, QFormLayout::ItemRole::FieldRole, lineGateKaidu);

        labelGateKaiduCount = new QLabel(groupGate);
        labelGateKaiduCount->setObjectName("labelGateKaiduCount");

        gateForm->setWidget(4, QFormLayout::ItemRole::LabelRole, labelGateKaiduCount);

        lineGateKaiduCount = new QLineEdit(groupGate);
        lineGateKaiduCount->setObjectName("lineGateKaiduCount");

        gateForm->setWidget(4, QFormLayout::ItemRole::FieldRole, lineGateKaiduCount);


        paramLayout->addWidget(groupGate);

        groupOta = new QGroupBox(tabParam);
        groupOta->setObjectName("groupOta");
        otaForm = new QFormLayout(groupOta);
        otaForm->setObjectName("otaForm");
        labelOtaFlag = new QLabel(groupOta);
        labelOtaFlag->setObjectName("labelOtaFlag");

        otaForm->setWidget(0, QFormLayout::ItemRole::LabelRole, labelOtaFlag);

        otaFlagRow = new QHBoxLayout();
        otaFlagRow->setObjectName("otaFlagRow");
        lineOtaFlag = new QLineEdit(groupOta);
        lineOtaFlag->setObjectName("lineOtaFlag");

        otaFlagRow->addWidget(lineOtaFlag);

        labelOtaFlagHex = new QLabel(groupOta);
        labelOtaFlagHex->setObjectName("labelOtaFlagHex");

        otaFlagRow->addWidget(labelOtaFlagHex);


        otaForm->setLayout(0, QFormLayout::ItemRole::FieldRole, otaFlagRow);

        labelOtaVer = new QLabel(groupOta);
        labelOtaVer->setObjectName("labelOtaVer");

        otaForm->setWidget(1, QFormLayout::ItemRole::LabelRole, labelOtaVer);

        lineOtaVer = new QLineEdit(groupOta);
        lineOtaVer->setObjectName("lineOtaVer");

        otaForm->setWidget(1, QFormLayout::ItemRole::FieldRole, lineOtaVer);

        labelOtaSize = new QLabel(groupOta);
        labelOtaSize->setObjectName("labelOtaSize");

        otaForm->setWidget(2, QFormLayout::ItemRole::LabelRole, labelOtaSize);

        lineOtaSize = new QLineEdit(groupOta);
        lineOtaSize->setObjectName("lineOtaSize");
        lineOtaSize->setReadOnly(true);

        otaForm->setWidget(2, QFormLayout::ItemRole::FieldRole, lineOtaSize);

        labelUpdateFlag = new QLabel(groupOta);
        labelUpdateFlag->setObjectName("labelUpdateFlag");

        otaForm->setWidget(3, QFormLayout::ItemRole::LabelRole, labelUpdateFlag);

        lineUpdateFlag = new QLineEdit(groupOta);
        lineUpdateFlag->setObjectName("lineUpdateFlag");

        otaForm->setWidget(3, QFormLayout::ItemRole::FieldRole, lineUpdateFlag);


        paramLayout->addWidget(groupOta);

        groupMqtt = new QGroupBox(tabParam);
        groupMqtt->setObjectName("groupMqtt");
        mqttForm = new QFormLayout(groupMqtt);
        mqttForm->setObjectName("mqttForm");
        labelMqttProductKey = new QLabel(groupMqtt);
        labelMqttProductKey->setObjectName("labelMqttProductKey");

        mqttForm->setWidget(0, QFormLayout::ItemRole::LabelRole, labelMqttProductKey);

        lineMqttProductKey = new QLineEdit(groupMqtt);
        lineMqttProductKey->setObjectName("lineMqttProductKey");
        lineMqttProductKey->setMaxLength(32);

        mqttForm->setWidget(0, QFormLayout::ItemRole::FieldRole, lineMqttProductKey);

        labelMqttDeviceKey = new QLabel(groupMqtt);
        labelMqttDeviceKey->setObjectName("labelMqttDeviceKey");

        mqttForm->setWidget(1, QFormLayout::ItemRole::LabelRole, labelMqttDeviceKey);

        lineMqttDeviceKey = new QLineEdit(groupMqtt);
        lineMqttDeviceKey->setObjectName("lineMqttDeviceKey");
        lineMqttDeviceKey->setMaxLength(32);

        mqttForm->setWidget(1, QFormLayout::ItemRole::FieldRole, lineMqttDeviceKey);

        labelMqttDeviceSecret = new QLabel(groupMqtt);
        labelMqttDeviceSecret->setObjectName("labelMqttDeviceSecret");

        mqttForm->setWidget(2, QFormLayout::ItemRole::LabelRole, labelMqttDeviceSecret);

        lineMqttDeviceSecret = new QLineEdit(groupMqtt);
        lineMqttDeviceSecret->setObjectName("lineMqttDeviceSecret");
        lineMqttDeviceSecret->setMaxLength(32);

        mqttForm->setWidget(2, QFormLayout::ItemRole::FieldRole, lineMqttDeviceSecret);

        labelMqttAddr = new QLabel(groupMqtt);
        labelMqttAddr->setObjectName("labelMqttAddr");

        mqttForm->setWidget(3, QFormLayout::ItemRole::LabelRole, labelMqttAddr);

        lineMqttAddr = new QLineEdit(groupMqtt);
        lineMqttAddr->setObjectName("lineMqttAddr");
        lineMqttAddr->setMaxLength(32);

        mqttForm->setWidget(3, QFormLayout::ItemRole::FieldRole, lineMqttAddr);


        paramLayout->addWidget(groupMqtt);

        labelParamHint = new QLabel(tabParam);
        labelParamHint->setObjectName("labelParamHint");
        labelParamHint->setWordWrap(true);

        paramLayout->addWidget(labelParamHint);

        paramRow1 = new QHBoxLayout();
        paramRow1->setObjectName("paramRow1");
        btnRead = new QPushButton(tabParam);
        btnRead->setObjectName("btnRead");

        paramRow1->addWidget(btnRead);

        btnWrite = new QPushButton(tabParam);
        btnWrite->setObjectName("btnWrite");

        paramRow1->addWidget(btnWrite);

        paramSpacer = new QSpacerItem(40, 20, QSizePolicy::Policy::Expanding, QSizePolicy::Policy::Minimum);

        paramRow1->addItem(paramSpacer);


        paramLayout->addLayout(paramRow1);

        paramSpacer2 = new QSpacerItem(20, 40, QSizePolicy::Policy::Minimum, QSizePolicy::Policy::Expanding);

        paramLayout->addItem(paramSpacer2);

        tabWidget->addTab(tabParam, QString());
        tab4g = new QWidget();
        tab4g->setObjectName("tab4g");
        g4Layout = new QVBoxLayout(tab4g);
        g4Layout->setObjectName("g4Layout");
        group4g = new QGroupBox(tab4g);
        group4g->setObjectName("group4g");
        g4Form = new QFormLayout(group4g);
        g4Form->setObjectName("g4Form");
        labelSockRstim = new QLabel(group4g);
        labelSockRstim->setObjectName("labelSockRstim");

        g4Form->setWidget(0, QFormLayout::ItemRole::LabelRole, labelSockRstim);

        lineSockRstim = new QLineEdit(group4g);
        lineSockRstim->setObjectName("lineSockRstim");

        g4Form->setWidget(0, QFormLayout::ItemRole::FieldRole, lineSockRstim);

        labelSockRsnum = new QLabel(group4g);
        labelSockRsnum->setObjectName("labelSockRsnum");

        g4Form->setWidget(1, QFormLayout::ItemRole::LabelRole, labelSockRsnum);

        lineSockRsnum = new QLineEdit(group4g);
        lineSockRsnum->setObjectName("lineSockRsnum");

        g4Form->setWidget(1, QFormLayout::ItemRole::FieldRole, lineSockRsnum);


        g4Layout->addWidget(group4g);

        label4gHint = new QLabel(tab4g);
        label4gHint->setObjectName("label4gHint");
        label4gHint->setWordWrap(true);

        g4Layout->addWidget(label4gHint);

        g4Row1 = new QHBoxLayout();
        g4Row1->setObjectName("g4Row1");
        btn4gPush = new QPushButton(tab4g);
        btn4gPush->setObjectName("btn4gPush");

        g4Row1->addWidget(btn4gPush);

        btn4gRead = new QPushButton(tab4g);
        btn4gRead->setObjectName("btn4gRead");

        g4Row1->addWidget(btn4gRead);

        btn4gStop = new QPushButton(tab4g);
        btn4gStop->setObjectName("btn4gStop");

        g4Row1->addWidget(btn4gStop);

        btnClear4g = new QPushButton(tab4g);
        btnClear4g->setObjectName("btnClear4g");

        g4Row1->addWidget(btnClear4g);

        g4Spacer = new QSpacerItem(40, 20, QSizePolicy::Policy::Expanding, QSizePolicy::Policy::Minimum);

        g4Row1->addItem(g4Spacer);


        g4Layout->addLayout(g4Row1);

        g4Row2 = new QHBoxLayout();
        g4Row2->setObjectName("g4Row2");
        labelProgress = new QLabel(tab4g);
        labelProgress->setObjectName("labelProgress");

        g4Row2->addWidget(labelProgress);

        progress4g = new QProgressBar(tab4g);
        progress4g->setObjectName("progress4g");
        progress4g->setValue(0);

        g4Row2->addWidget(progress4g);


        g4Layout->addLayout(g4Row2);

        label4gState = new QLabel(tab4g);
        label4gState->setObjectName("label4gState");

        g4Layout->addWidget(label4gState);

        text4gView = new QPlainTextEdit(tab4g);
        text4gView->setObjectName("text4gView");
        text4gView->setReadOnly(true);

        g4Layout->addWidget(text4gView);

        tabWidget->addTab(tab4g, QString());
        tabBle = new QWidget();
        tabBle->setObjectName("tabBle");
        bleLayout = new QVBoxLayout(tabBle);
        bleLayout->setObjectName("bleLayout");
        groupBle = new QGroupBox(tabBle);
        groupBle->setObjectName("groupBle");
        bleForm = new QFormLayout(groupBle);
        bleForm->setObjectName("bleForm");
        labelBleAddr = new QLabel(groupBle);
        labelBleAddr->setObjectName("labelBleAddr");

        bleForm->setWidget(0, QFormLayout::ItemRole::LabelRole, labelBleAddr);

        lineBleAddr = new QLineEdit(groupBle);
        lineBleAddr->setObjectName("lineBleAddr");
        lineBleAddr->setReadOnly(true);

        bleForm->setWidget(0, QFormLayout::ItemRole::FieldRole, lineBleAddr);

        labelBleName = new QLabel(groupBle);
        labelBleName->setObjectName("labelBleName");

        bleForm->setWidget(1, QFormLayout::ItemRole::LabelRole, labelBleName);

        lineBleName = new QLineEdit(groupBle);
        lineBleName->setObjectName("lineBleName");
        lineBleName->setReadOnly(true);

        bleForm->setWidget(1, QFormLayout::ItemRole::FieldRole, lineBleName);

        labelBleVersion = new QLabel(groupBle);
        labelBleVersion->setObjectName("labelBleVersion");

        bleForm->setWidget(2, QFormLayout::ItemRole::LabelRole, labelBleVersion);

        lineBleVersion = new QLineEdit(groupBle);
        lineBleVersion->setObjectName("lineBleVersion");
        lineBleVersion->setReadOnly(true);

        bleForm->setWidget(2, QFormLayout::ItemRole::FieldRole, lineBleVersion);


        bleLayout->addWidget(groupBle);

        labelBleHint = new QLabel(tabBle);
        labelBleHint->setObjectName("labelBleHint");
        labelBleHint->setWordWrap(true);

        bleLayout->addWidget(labelBleHint);

        bleRow1 = new QHBoxLayout();
        bleRow1->setObjectName("bleRow1");
        btnBleAddr = new QPushButton(tabBle);
        btnBleAddr->setObjectName("btnBleAddr");

        bleRow1->addWidget(btnBleAddr);

        btnBleName = new QPushButton(tabBle);
        btnBleName->setObjectName("btnBleName");

        bleRow1->addWidget(btnBleName);

        btnBleVersion = new QPushButton(tabBle);
        btnBleVersion->setObjectName("btnBleVersion");

        bleRow1->addWidget(btnBleVersion);

        btnBleClearLog = new QPushButton(tabBle);
        btnBleClearLog->setObjectName("btnBleClearLog");

        bleRow1->addWidget(btnBleClearLog);

        bleSpacer1 = new QSpacerItem(40, 20, QSizePolicy::Policy::Expanding, QSizePolicy::Policy::Minimum);

        bleRow1->addItem(bleSpacer1);


        bleLayout->addLayout(bleRow1);

        bleRow2 = new QHBoxLayout();
        bleRow2->setObjectName("bleRow2");
        labelBleCmd = new QLabel(tabBle);
        labelBleCmd->setObjectName("labelBleCmd");

        bleRow2->addWidget(labelBleCmd);

        lineBleCmd = new QLineEdit(tabBle);
        lineBleCmd->setObjectName("lineBleCmd");

        bleRow2->addWidget(lineBleCmd);

        btnBleSend = new QPushButton(tabBle);
        btnBleSend->setObjectName("btnBleSend");

        bleRow2->addWidget(btnBleSend);


        bleLayout->addLayout(bleRow2);

        textBleView = new QPlainTextEdit(tabBle);
        textBleView->setObjectName("textBleView");
        textBleView->setReadOnly(true);

        bleLayout->addWidget(textBleView);

        tabWidget->addTab(tabBle, QString());
        tabAt = new QWidget();
        tabAt->setObjectName("tabAt");
        atLayout = new QVBoxLayout(tabAt);
        atLayout->setObjectName("atLayout");
        atRow1 = new QHBoxLayout();
        atRow1->setObjectName("atRow1");
        labelAtCmd = new QLabel(tabAt);
        labelAtCmd->setObjectName("labelAtCmd");

        atRow1->addWidget(labelAtCmd);

        lineAtCmd = new QLineEdit(tabAt);
        lineAtCmd->setObjectName("lineAtCmd");

        atRow1->addWidget(lineAtCmd);

        btnAtSend = new QPushButton(tabAt);
        btnAtSend->setObjectName("btnAtSend");

        atRow1->addWidget(btnAtSend);


        atLayout->addLayout(atRow1);

        atRow2 = new QHBoxLayout();
        atRow2->setObjectName("atRow2");
        btnAtCsq = new QPushButton(tabAt);
        btnAtCsq->setObjectName("btnAtCsq");

        atRow2->addWidget(btnAtCsq);

        btnAtIccid = new QPushButton(tabAt);
        btnAtIccid->setObjectName("btnAtIccid");

        atRow2->addWidget(btnAtIccid);

        btnAtCclk = new QPushButton(tabAt);
        btnAtCclk->setObjectName("btnAtCclk");

        atRow2->addWidget(btnAtCclk);

        btnAtMqttCid = new QPushButton(tabAt);
        btnAtMqttCid->setObjectName("btnAtMqttCid");

        atRow2->addWidget(btnAtMqttCid);

        btnClearAt = new QPushButton(tabAt);
        btnClearAt->setObjectName("btnClearAt");

        atRow2->addWidget(btnClearAt);

        atSpacer = new QSpacerItem(40, 20, QSizePolicy::Policy::Expanding, QSizePolicy::Policy::Minimum);

        atRow2->addItem(atSpacer);


        atLayout->addLayout(atRow2);

        textAtView = new QPlainTextEdit(tabAt);
        textAtView->setObjectName("textAtView");
        textAtView->setReadOnly(true);

        atLayout->addWidget(textAtView);

        tabWidget->addTab(tabAt, QString());
        tabLog = new QWidget();
        tabLog->setObjectName("tabLog");
        logLayout = new QVBoxLayout(tabLog);
        logLayout->setObjectName("logLayout");
        logRow1 = new QHBoxLayout();
        logRow1->setObjectName("logRow1");
        checkHex = new QCheckBox(tabLog);
        checkHex->setObjectName("checkHex");

        logRow1->addWidget(checkHex);

        btnClearLog = new QPushButton(tabLog);
        btnClearLog->setObjectName("btnClearLog");

        logRow1->addWidget(btnClearLog);

        btnExportLog = new QPushButton(tabLog);
        btnExportLog->setObjectName("btnExportLog");

        logRow1->addWidget(btnExportLog);

        logSpacer = new QSpacerItem(40, 20, QSizePolicy::Policy::Expanding, QSizePolicy::Policy::Minimum);

        logRow1->addItem(logSpacer);


        logLayout->addLayout(logRow1);

        textLogView = new QPlainTextEdit(tabLog);
        textLogView->setObjectName("textLogView");
        textLogView->setReadOnly(true);

        logLayout->addWidget(textLogView);

        tabWidget->addTab(tabLog, QString());

        centralLayout->addWidget(tabWidget);

        MainWindow->setCentralWidget(centralwidget);
        statusbar = new QStatusBar(MainWindow);
        statusbar->setObjectName("statusbar");
        MainWindow->setStatusBar(statusbar);

        retranslateUi(MainWindow);

        tabWidget->setCurrentIndex(0);


        QMetaObject::connectSlotsByName(MainWindow);
    } // setupUi

    void retranslateUi(QMainWindow *MainWindow)
    {
        MainWindow->setWindowTitle(QCoreApplication::translate("MainWindow", "\344\270\213\344\275\215\346\234\272\345\210\235\345\247\213\345\214\226\351\205\215\347\275\256\345\267\245\345\205\267", nullptr));
        labelPortName->setText(QCoreApplication::translate("MainWindow", "\344\270\262\345\217\243\345\217\267\357\274\232", nullptr));
        btnRefresh->setText(QCoreApplication::translate("MainWindow", "\345\210\267\346\226\260", nullptr));
        labelBaud->setText(QCoreApplication::translate("MainWindow", "\346\263\242\347\211\271\347\216\207\357\274\232", nullptr));
        comboBaud->setItemText(0, QCoreApplication::translate("MainWindow", "9600", nullptr));
        comboBaud->setItemText(1, QCoreApplication::translate("MainWindow", "115200", nullptr));
        comboBaud->setItemText(2, QCoreApplication::translate("MainWindow", "460800", nullptr));

        btnOpen->setText(QCoreApplication::translate("MainWindow", "\346\211\223\345\274\200\344\270\262\345\217\243", nullptr));
        btnProbe->setText(QCoreApplication::translate("MainWindow", "\351\223\276\350\267\257\346\216\242\346\264\273", nullptr));
        labelState->setText(QCoreApplication::translate("MainWindow", "\346\234\252\350\277\236\346\216\245", nullptr));
        labelSerialHint->setText(QCoreApplication::translate("MainWindow", "\344\270\213\344\275\215\346\234\272\345\233\272\345\256\232\344\275\277\347\224\250 115200 8N1\357\274\210\346\227\240\346\265\201\346\216\247\357\274\211\343\200\202\346\263\242\347\211\271\347\216\207\344\270\213\346\213\211\346\241\206\351\273\230\350\256\244 115200\357\274\214\345\205\266\345\256\203\345\200\274\344\273\205\347\224\250\344\272\216\346\216\222\346\237\245\346\216\245\347\272\277\351\227\256\351\242\230\343\200\202", nullptr));
        tabWidget->setTabText(tabWidget->indexOf(tabSerial), QCoreApplication::translate("MainWindow", "\344\270\262\345\217\243\350\277\236\346\216\245", nullptr));
        groupGate->setTitle(QCoreApplication::translate("MainWindow", "\351\227\270\351\227\250\345\217\202\346\225\260\357\274\210EEPROM 0x0004\342\200\2230x000C\357\274\211", nullptr));
        labelMotorWarningCurrUpper->setText(QCoreApplication::translate("MainWindow", "\345\240\265\350\275\254\347\224\265\346\265\201\344\270\212\351\231\220\357\274\2100.1A\357\274\211\357\274\232", nullptr));
        labelGateMaxheight->setText(QCoreApplication::translate("MainWindow", "\351\227\270\351\227\250\345\205\250\345\274\200\351\253\230\345\272\246\357\274\210mm\357\274\211\357\274\232", nullptr));
        labelGateMaxturns->setText(QCoreApplication::translate("MainWindow", "\351\207\217\347\250\213\345\234\210\346\225\260\357\274\210\350\236\272\347\272\271\345\234\210\346\225\260\357\274\211\357\274\232", nullptr));
        labelGateKaidu->setText(QCoreApplication::translate("MainWindow", "\345\274\200\345\272\246\347\231\276\345\210\206\346\257\224\357\274\2100-100\357\274\211\357\274\232", nullptr));
        labelGateKaiduCount->setText(QCoreApplication::translate("MainWindow", "\345\274\200\345\272\246\350\256\241\346\225\260\357\274\232", nullptr));
        groupOta->setTitle(QCoreApplication::translate("MainWindow", "OTA \344\277\241\346\201\257\357\274\210EEPROM 200\342\200\223243\357\274\214\346\225\264\345\235\227\347\273\223\346\236\204\344\275\223\357\274\211", nullptr));
        labelOtaFlag->setText(QCoreApplication::translate("MainWindow", "OTA_flag\357\274\232", nullptr));
        labelOtaFlagHex->setText(QCoreApplication::translate("MainWindow", "= -", nullptr));
        labelOtaVer->setText(QCoreApplication::translate("MainWindow", "OTA_ver\357\274\232", nullptr));
        labelOtaSize->setText(QCoreApplication::translate("MainWindow", "OTA_size\357\274\210\345\217\252\350\257\273\357\274\211\357\274\232", nullptr));
        labelUpdateFlag->setText(QCoreApplication::translate("MainWindow", "UPDATE_flag\357\274\2100/1\357\274\211\357\274\232", nullptr));
        groupMqtt->setTitle(QCoreApplication::translate("MainWindow", "MQTT \345\217\202\346\225\260\357\274\210EEPROM 64/96/128/160\357\274\214\345\220\204 32 \345\255\227\347\254\246\357\274\211", nullptr));
        labelMqttProductKey->setText(QCoreApplication::translate("MainWindow", "productKey\357\274\232", nullptr));
        labelMqttDeviceKey->setText(QCoreApplication::translate("MainWindow", "deviceKey\357\274\232", nullptr));
        labelMqttDeviceSecret->setText(QCoreApplication::translate("MainWindow", "deviceSecret\357\274\232", nullptr));
        labelMqttAddr->setText(QCoreApplication::translate("MainWindow", "mqtt_addr\357\274\232", nullptr));
        labelParamHint->setText(QCoreApplication::translate("MainWindow", "\350\257\264\346\230\216\357\274\232\350\277\231\351\207\214\346\230\257\344\270\213\344\275\215\346\234\272 EEPROM \344\270\255\347\232\204\345\217\202\346\225\260\357\274\210\351\227\270\351\227\250 5 \351\241\271 + OTA 4 \351\241\271 + MQTT 4 \351\241\271\357\274\211\343\200\202\345\273\272\350\256\256\345\205\210\342\200\234\350\257\273\345\217\226\345\217\202\346\225\260\342\200\235\345\206\215\344\277\256\346\224\271\357\274\233\342\200\234\345\206\231\345\205\245\345\217\202\346\225\260\342\200\235\345\217\252\344\274\232\344\270\213\345\217\221\344\270\216\344\270\212\346\254\241\350\257\273\345\217\226\344\270\215\345\220\214\347\232\204\345\255\227\346\256\265\343\200\202", nullptr));
        btnRead->setText(QCoreApplication::translate("MainWindow", "\350\257\273\345\217\226\345\217\202\346\225\260", nullptr));
        btnWrite->setText(QCoreApplication::translate("MainWindow", "\345\206\231\345\205\245\345\217\202\346\225\260", nullptr));
        tabWidget->setTabText(tabWidget->indexOf(tabParam), QCoreApplication::translate("MainWindow", "\345\217\202\346\225\260\351\205\215\347\275\256", nullptr));
        group4g->setTitle(QCoreApplication::translate("MainWindow", "\351\207\215\350\277\236\347\255\226\347\225\245\357\274\210\345\217\252\344\270\213\345\217\221\345\210\260 4G \346\250\241\345\235\227\357\274\214\344\270\215\345\206\231\345\205\245\344\270\213\344\275\215\346\234\272 EEPROM\357\274\211", nullptr));
        labelSockRstim->setText(QCoreApplication::translate("MainWindow", "\351\207\215\350\277\236\351\227\264\351\232\224\357\274\210\347\247\222\357\274\211\357\274\232", nullptr));
        labelSockRsnum->setText(QCoreApplication::translate("MainWindow", "\351\207\215\350\277\236\346\254\241\346\225\260\357\274\232", nullptr));
        label4gHint->setText(QCoreApplication::translate("MainWindow", "\350\257\264\346\230\216\357\274\232\344\270\200\351\224\256\351\205\215\347\275\256\344\274\232\350\257\273\345\217\226\343\200\214\345\217\202\346\225\260\351\205\215\347\275\256\343\200\215\351\241\265 MQTT \347\273\204\347\232\204\345\233\233\351\241\271\357\274\210productKey/deviceKey/deviceSecret/mqtt_addr\357\274\214\346\235\245\350\207\252\344\270\213\344\275\215\346\234\272 EEPROM\357\274\211\357\274\214\351\200\220\346\235\241\344\270\213\345\217\221 AT \346\214\207\344\273\244\345\210\260 4G \346\250\241\345\235\227\357\274\233productKey \345\234\250\346\250\241\345\235\227\351\207\214\346\262\241\346\234\211\345\257\271\345\272\224\345\217\202\346\225\260\357\274\214\345\217\252\344\275\223\347\216\260\345\234\250 13 \346\235\241 topic \346\214\207\344\273\244\344\270\255\357\274\233mqtt_addr \346\224\257\346\214\201\342\200\234\345\234\260\345\235\200:\347\253\257\345\217\243\342\200\235\346\210\226\342\200\234\345\234\260\345\235\200,\347\253\257\345\217\243\342\200\235\343\200\202\344\270\213\346"
                        "\226\271\351\207\215\350\277\236\351\227\264\351\232\224/\346\254\241\346\225\260\345\217\252\347\224\250\344\272\216\346\234\254\346\254\241\344\270\213\345\217\221\357\274\210\344\270\215\345\206\231\345\205\245\344\270\213\344\275\215\346\234\272 EEPROM\357\274\211\357\274\232\347\225\231\347\251\272\350\241\250\347\244\272\346\234\254\346\254\241\344\270\215\344\270\213\345\217\221\357\274\210\346\250\241\345\235\227\344\277\235\346\214\201\345\216\237\345\200\274\357\274\211\357\274\214\345\241\253\344\272\206\345\210\231\344\270\272 1\342\200\22365535\357\274\2100 \344\274\232\350\242\253\346\250\241\345\235\227\346\213\222\347\273\235\357\274\211\357\274\214\343\200\214\345\205\250\351\203\250\350\257\273\345\217\226\343\200\215\344\274\232\350\277\236\350\277\231\344\270\244\351\241\271\344\270\200\350\265\267\344\273\216\346\250\241\345\235\227\350\257\273\345\233\236\357\274\210\346\250\241\345\235\227\346\224\257\346\214\201 AT+SOCKRSTIM? / AT+SOCKRSNUM?\357\274\211\343\200\202", nullptr));
        btn4gPush->setText(QCoreApplication::translate("MainWindow", "\345\205\250\351\203\250\344\270\213\345\217\221\357\274\210\344\270\200\351\224\256\351\205\215\347\275\256\357\274\211", nullptr));
        btn4gRead->setText(QCoreApplication::translate("MainWindow", "\345\205\250\351\203\250\350\257\273\345\217\226", nullptr));
        btn4gStop->setText(QCoreApplication::translate("MainWindow", "\345\201\234\346\255\242", nullptr));
        btnClear4g->setText(QCoreApplication::translate("MainWindow", "\346\270\205\347\251\272\346\227\245\345\277\227", nullptr));
        labelProgress->setText(QCoreApplication::translate("MainWindow", "\351\205\215\347\275\256\350\277\233\345\272\246\357\274\232", nullptr));
        label4gState->setText(QCoreApplication::translate("MainWindow", "\347\212\266\346\200\201\357\274\232\347\251\272\351\227\262", nullptr));
        tabWidget->setTabText(tabWidget->indexOf(tab4g), QCoreApplication::translate("MainWindow", "4G \351\205\215\347\275\256", nullptr));
        groupBle->setTitle(QCoreApplication::translate("MainWindow", "\350\223\235\347\211\231\346\250\241\345\235\227\357\274\210DX-BT24\357\274\214USART3 \342\206\222 PD8/PD9\357\274\2149600 8N1\357\274\211", nullptr));
        labelBleAddr->setText(QCoreApplication::translate("MainWindow", "\350\223\235\347\211\231\345\234\260\345\235\200\357\274\232", nullptr));
        labelBleName->setText(QCoreApplication::translate("MainWindow", "\350\223\235\347\211\231\345\220\215\357\274\232", nullptr));
        labelBleVersion->setText(QCoreApplication::translate("MainWindow", "\347\211\210\346\234\254\345\217\267\357\274\232", nullptr));
        labelBleHint->setText(QCoreApplication::translate("MainWindow", "\350\257\264\346\230\216\357\274\232\350\257\273\345\217\226\346\230\257\351\200\232\350\277\207\344\270\213\344\275\215\346\234\272\346\212\212 AT \346\214\207\344\273\244\351\200\217\344\274\240\345\210\260\350\223\235\347\211\231\346\250\241\345\235\227\357\274\210AT+LADDR / AT+NAME / AT+VERSION\357\274\211\343\200\202\346\250\241\345\235\227\345\217\252\345\234\250\350\223\235\347\211\231\346\234\252\350\242\253\350\277\236\346\216\245\346\227\266\346\211\215\350\277\233 AT \346\250\241\345\274\217\342\200\224\342\200\224\350\213\245\346\211\213\346\234\272\346\255\243\350\277\236\347\235\200\357\274\214\350\257\273\345\217\226\344\274\232\350\266\205\346\227\266\357\274\214\350\257\267\345\205\210\346\226\255\345\274\200\346\211\213\346\234\272\357\274\233\350\213\245\344\270\200\347\233\264\350\266\205\346\227\266\357\274\214\346\243\200\346\237\245\346\250\241\345\235\227\346\230\257\345\220\246\350\243\205\345\245\275\344\270\216 PD8(TX)/PD9(RX) \346\216\245\347\272\277\343\200\202", nullptr));
        btnBleAddr->setText(QCoreApplication::translate("MainWindow", "\350\257\273\345\217\226\350\223\235\347\211\231\345\234\260\345\235\200", nullptr));
        btnBleName->setText(QCoreApplication::translate("MainWindow", "\350\257\273\345\217\226\350\223\235\347\211\231\345\220\215", nullptr));
        btnBleVersion->setText(QCoreApplication::translate("MainWindow", "\350\257\273\345\217\226\347\211\210\346\234\254", nullptr));
        btnBleClearLog->setText(QCoreApplication::translate("MainWindow", "\346\270\205\347\251\272\346\227\245\345\277\227", nullptr));
        labelBleCmd->setText(QCoreApplication::translate("MainWindow", "AT \346\214\207\344\273\244\357\274\232", nullptr));
        lineBleCmd->setPlaceholderText(QCoreApplication::translate("MainWindow", "\344\276\213\345\246\202 AT+LADDR\357\274\210\346\227\240\351\234\200\350\276\223\345\205\245\347\273\223\345\260\276\347\232\204 CR/LF\357\274\211", nullptr));
        btnBleSend->setText(QCoreApplication::translate("MainWindow", "\345\217\221\351\200\201", nullptr));
        tabWidget->setTabText(tabWidget->indexOf(tabBle), QCoreApplication::translate("MainWindow", "\350\223\235\347\211\231", nullptr));
        labelAtCmd->setText(QCoreApplication::translate("MainWindow", "AT \346\214\207\344\273\244\357\274\232", nullptr));
        lineAtCmd->setPlaceholderText(QCoreApplication::translate("MainWindow", "\344\276\213\345\246\202 usr.cn#AT+CSQ?\357\274\210WH-LTE-7S1 \351\234\200\350\246\201 usr.cn# \345\211\215\347\274\200\357\274\211", nullptr));
        btnAtSend->setText(QCoreApplication::translate("MainWindow", "\345\217\221\351\200\201", nullptr));
        btnAtCsq->setText(QCoreApplication::translate("MainWindow", "\344\277\241\345\217\267\350\264\250\351\207\217 AT+CSQ", nullptr));
        btnAtIccid->setText(QCoreApplication::translate("MainWindow", "SIM\345\215\241 AT+ICCID", nullptr));
        btnAtCclk->setText(QCoreApplication::translate("MainWindow", "\346\250\241\345\235\227\346\227\266\351\227\264 AT+CCLK", nullptr));
        btnAtMqttCid->setText(QCoreApplication::translate("MainWindow", "MQTT ID AT+MQTTCID", nullptr));
        btnClearAt->setText(QCoreApplication::translate("MainWindow", "\346\270\205\347\251\272\346\227\245\345\277\227", nullptr));
        tabWidget->setTabText(tabWidget->indexOf(tabAt), QCoreApplication::translate("MainWindow", "AT \346\216\247\345\210\266\345\217\260", nullptr));
        checkHex->setText(QCoreApplication::translate("MainWindow", "HEX \346\230\276\347\244\272", nullptr));
        btnClearLog->setText(QCoreApplication::translate("MainWindow", "\346\270\205\347\251\272", nullptr));
        btnExportLog->setText(QCoreApplication::translate("MainWindow", "\345\257\274\345\207\272\346\227\245\345\277\227", nullptr));
        tabWidget->setTabText(tabWidget->indexOf(tabLog), QCoreApplication::translate("MainWindow", "\346\227\245\345\277\227", nullptr));
    } // retranslateUi

};

namespace Ui {
    class MainWindow: public Ui_MainWindow {};
} // namespace Ui

QT_END_NAMESPACE

#endif // UI_MAINWINDOW_H
