/*
 * Turbo Vision headless console adapter internals.
 * Copyright (c) 2026 by Christian Klukas
 * Licensed under the MIT License.
 */
#ifndef TVISION_INTERNAL_HEADLESS_H
#define TVISION_INTERNAL_HEADLESS_H

#include <internal/platform.h>

namespace tvision
{

// Returns null when TVISION_HEADLESS_FD is absent or invalid. The returned
// adapter owns all of its display/input/session objects.
ConsoleAdapter *createHeadlessConsole() noexcept;

} // namespace tvision

#endif // TVISION_INTERNAL_HEADLESS_H
