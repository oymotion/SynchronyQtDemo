#include <QApplication>
#include <QStyle>
#include <QStyleFactory>

#include <sensorcpp.hpp>

#ifdef Q_OS_ANDROID
#include <QJniObject>
#endif

#include "MainWindow.h"
#include "SdkBridge.h"

int main(int argc, char* argv[]) {
    // Register the metatypes for the queued signals.
    qRegisterMetaType<DeviceEntry>("DeviceEntry");
    qRegisterMetaType<QVector<DeviceEntry>>("QVector<DeviceEntry>");
    qRegisterMetaType<QSharedPointer<sensor::SensorData>>("QSharedPointer<sensor::SensorData>");

    QApplication app(argc, argv);

    // Force a consistent light theme on every platform.
    QApplication::setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
    QApplication::setPalette(QApplication::style()->standardPalette());

#ifdef Q_OS_ANDROID
    // Register the Java BLE bridge before any SDK BLE operation.
    auto ctx = QNativeInterface::QAndroidApplication::context();
    const QJniObject ctxObj(ctx.object<jobject>());
    QJniObject bridge("com/oymotion/sensor/ble/SensorBleBridge",
                      "(Landroid/content/Context;)V", ctx.object());
    bridge.callMethod<void>("setBridge");
    // Ask for the runtime BLE permissions before the first scan.
    bridge.callMethod<jboolean>("requestBlePermissions");
    // Request all-files storage access for the SDK session logs and the .bin
    // export under /sdcard/Documents/sensorsdklog.
    if (QNativeInterface::QAndroidApplication::sdkVersion() >= 30) {
        const bool storageGranted = QJniObject::callStaticMethod<jboolean>(
            "android/os/Environment", "isExternalStorageManager", "()Z");
        if (!storageGranted) {
            const QJniObject action = QJniObject::getStaticObjectField<jstring>(
                "android/provider/Settings", "ACTION_MANAGE_APP_ALL_FILES_ACCESS_PERMISSION");
            QJniObject intent("android/content/Intent", "(Ljava/lang/String;)V", action.object<jstring>());
            const QJniObject pkgName = ctxObj.callObjectMethod<jstring>("getPackageName");
            const QJniObject uri = QJniObject::callStaticObjectMethod(
                "android/net/Uri", "parse", "(Ljava/lang/String;)Landroid/net/Uri;",
                QJniObject::fromString(QStringLiteral("package:") + pkgName.toString()).object<jstring>());
            intent.callObjectMethod("setData", "(Landroid/net/Uri;)Landroid/content/Intent;", uri.object<jobject>());
            intent.callObjectMethod("addFlags", "(I)Landroid/content/Intent;", 0x10000000 /* FLAG_ACTIVITY_NEW_TASK */);
            ctxObj.callMethod<void>("startActivity", "(Landroid/content/Intent;)V", intent.object<jobject>());
        }
    }
#endif

    MainWindow window;
    window.show();

    // Background: flush the SDK capture and log files.
    QObject::connect(&app, &QGuiApplication::applicationStateChanged, [&window](Qt::ApplicationState state) {
        if (state == Qt::ApplicationSuspended) {
            window.onApplicationSuspended();
        }
    });

    return QApplication::exec();
}
