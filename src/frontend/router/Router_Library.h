#pragma once
#include "../FrontendRpc.h"

namespace HDAW { class FileLibraryManager; }
class QJsonValue;
class QString;

namespace frontend {
DispatchResult dispatchLibrary(HDAW::FileLibraryManager& lib, const QString& subMethod,
                               const QJsonValue& params);
}
