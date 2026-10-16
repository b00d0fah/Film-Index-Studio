#pragma once
#include <QString>

// One interactive instance per Windows logon session, independent of install path/version.
class SingleInstance {
  public:
    SingleInstance();
    ~SingleInstance();
    SingleInstance(const SingleInstance &) = delete;
    SingleInstance &operator=(const SingleInstance &) = delete;
    bool isPrimary() const {
        return primary;
    }
    const QString &error() const {
        return failure;
    }
    bool takeActivationRequest();

  private:
    void *mutex = nullptr;
    void *activationEvent = nullptr;
    bool primary = false;
    QString failure;
};
