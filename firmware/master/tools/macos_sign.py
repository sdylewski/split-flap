# macOS (Apple Silicon) kills unsigned binaries with SIGKILL. The native test binary from older
# Command Line Tools isn't signed, so ad-hoc sign it right after linking.
Import("env")
import sys

if sys.platform == "darwin":
    env.AddPostAction("$BUILD_DIR/${PROGNAME}$PROGSUFFIX", "codesign -f -s - $TARGET")
