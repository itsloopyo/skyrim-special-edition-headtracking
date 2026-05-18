#include "pch.h"
#include "notification.h"
#include "core/logger.h"

namespace SkyrimHT {

void ShowNotification(const char* message) {
    Logger::Instance().Info("Notification: %s", message);
}

} // namespace SkyrimHT
