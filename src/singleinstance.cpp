#include "singleinstance.h"
#ifdef Q_OS_WIN
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

SingleInstance::SingleInstance() {
#ifdef Q_OS_WIN
    mutex = CreateMutexW(nullptr, FALSE, L"Local\\FilmIndexStudio.Desktop.Singleton");
    if (!mutex) {
        failure = QString("无法创建单实例锁（Windows 错误 %1）。").arg(GetLastError());
        return;
    }
    const bool existing = GetLastError() == ERROR_ALREADY_EXISTS;
    activationEvent = CreateEventW(nullptr, FALSE, FALSE, L"Local\\FilmIndexStudio.Desktop.Activate");
    if (!activationEvent) {
        failure = QString("无法创建窗口激活事件（Windows 错误 %1）。").arg(GetLastError());
        return;
    }
    primary = !existing;
    if (existing)
        SetEvent(activationEvent);
#else
    primary = true;
#endif
}
SingleInstance::~SingleInstance() {
#ifdef Q_OS_WIN
    if (activationEvent)
        CloseHandle(activationEvent);
    if (mutex)
        CloseHandle(mutex);
#endif
}
bool SingleInstance::takeActivationRequest() {
#ifdef Q_OS_WIN
    return activationEvent && WaitForSingleObject(activationEvent, 0) == WAIT_OBJECT_0;
#else
    return false;
#endif
}
