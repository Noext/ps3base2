#pragma once

/* Where the loader gets its files, portably.
 *
 * Native reads the host filesystem. A browser cannot: the disc is served over
 * HTTP Range and the executable arrives as a fetch. Rather than teach the loader
 * about the network, it asks this hook first and only falls back to fopen.
 *
 * This is the seam milestone 4 needs: the page registers a provider that answers
 * from a fetch or a preloaded buffer, and the loader code path does not change
 * again.
 */
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace files {

using Provider = std::function<bool(const std::string& path, std::vector<uint8_t>& out)>;

/* Install a provider. Called before the loader runs. */
void set_provider(Provider fn);

/* Read a path: provider first, filesystem second. Empty means failure. */
std::vector<uint8_t> read(const std::string& path);

/* True if a provider is installed. */
bool has_provider();

}  // namespace files
