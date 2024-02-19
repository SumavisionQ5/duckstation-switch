#include "platform_misc.h"

namespace PlatformMisc {

// we can technically suspend the screen saver on Switch
// but I think it works perfectly fine for games
void SuspendScreensaver()
{}

void ResumeScreensaver()
{}

bool PlaySoundAsync(const char* path)
{
    // I probably need to come up with something awful to mix it in with audren
    return false;
}

} // namespace PlatformMisc
