#ifndef SHIPYARD_ENVIRONMENT_APP_DIRECTORIES_H
#define SHIPYARD_ENVIRONMENT_APP_DIRECTORIES_H

#include <string>

namespace Shipyard {
namespace Environment {

// The title's installed files and writable launch area share the game mount.
// A consumer can choose a separate save container for its persistent data.
inline std::string BundleDirectory() { return "game:"; }
inline std::string ApplicationDirectory() { return "game:"; }

} // namespace Environment
} // namespace Shipyard

#endif
