# =============================================================================
# version.mk - NE301 Firmware Version Management
# =============================================================================
# Version Format: MAJOR.MINOR.PATCH.BUILD
#   MAJOR: Incompatible architecture/protocol changes (manual update)
#   MINOR: Feature additions, backward compatible (manual update)
#   PATCH: Bug fixes (manual update)
#   BUILD: Auto-generated from git commit count (or CI/command line override)
#
# Usage:
#   make all                    # BUILD auto-generated from git commits
#   make all VERSION_BUILD=125  # Override BUILD number manually
#
# BUILD number priority:
#   1. Command line: VERSION_BUILD=xxx
#   2. CI environment: CI_PIPELINE_IID, BUILD_NUMBER, GITHUB_RUN_NUMBER
#   3. Git commit count (automatic)
#
# Reset BUILD count for new version:
#   git tag v2.3.1-base   # Creates baseline, BUILD starts from 0
# =============================================================================

# Main version number definition (manually update for releases)
VERSION_MAJOR  := 1
VERSION_MINOR  := 0
VERSION_PATCH  := 2
# Version suffix (optional, for alpha/beta/rc releases)
# Examples: alpha, beta, rc1, dev
# Leave empty for production releases
VERSION_SUFFIX := 

# =============================================================================
# Build number auto-generation
# =============================================================================
# Priority: Command line > CI environment > Git commit count
#
# Auto-generation based on git commit count from a baseline tag/commit.
# Each commit increments the build number, ensuring uniqueness.
#
# To reset build count: create a tag "v$(VERSION_MAJOR).$(VERSION_MINOR).$(VERSION_PATCH)-base"
# =============================================================================

# Check if VERSION_BUILD is provided via command line or CI
ifndef VERSION_BUILD
  # Try CI environment variables first
  ifdef CI_PIPELINE_IID
    VERSION_BUILD := $(CI_PIPELINE_IID)
  else ifdef BUILD_NUMBER
    VERSION_BUILD := $(BUILD_NUMBER)
  else ifdef GITHUB_RUN_NUMBER
    VERSION_BUILD := $(GITHUB_RUN_NUMBER)
  else
    # Auto-generate from git commit count
    # Count commits since the version baseline tag, or all commits if no tag exists
    VERSION_BASE_TAG := v$(VERSION_MAJOR).$(VERSION_MINOR).$(VERSION_PATCH)-base
    GIT_BUILD_COUNT := $(shell git rev-list --count $(VERSION_BASE_TAG)..HEAD 2>/dev/null || git rev-list --count HEAD 2>/dev/null || echo 0)
    VERSION_BUILD := $(GIT_BUILD_COUNT)
  endif
endif

# Full version string with optional suffix
VERSION := $(VERSION_MAJOR).$(VERSION_MINOR).$(VERSION_PATCH).$(VERSION_BUILD)
ifneq ($(VERSION_SUFFIX),)
  VERSION_WITH_SUFFIX := $(VERSION)_$(VERSION_SUFFIX)
else
  VERSION_WITH_SUFFIX := $(VERSION)
endif

# =============================================================================
# Component versions (can be independently set, defaults to main version)
# =============================================================================
# Set component-specific versions (leave empty to use main version)
# Format: MAJOR.MINOR.PATCH.BUILD or empty
FSBL_VERSION_OVERRIDE    := 1.0.0.3
APP_VERSION_OVERRIDE     := $(VERSION)
WEB_VERSION_OVERRIDE     := 1.3.4.6
MODEL_VERSION_OVERRIDE   := 1.0.0.0
WAKECORE_VERSION_OVERRIDE := 0.2.7.3

# Set component-specific version suffixes
# - Leave undefined (commented or not set) to inherit main suffix
# - Set to empty string (NONE) to explicitly disable suffix for this component
# - Set to any value to use that suffix
FSBL_SUFFIX    := NONE
APP_SUFFIX     := NONE
WEB_SUFFIX     := NONE
MODEL_SUFFIX   := NONE
WAKECORE_SUFFIX := NONE

# Build final component versions
FSBL_VERSION     := $(if $(FSBL_VERSION_OVERRIDE),$(FSBL_VERSION_OVERRIDE),$(VERSION))
APP_VERSION      := $(if $(APP_VERSION_OVERRIDE),$(APP_VERSION_OVERRIDE),$(VERSION))
WEB_VERSION      := $(if $(WEB_VERSION_OVERRIDE),$(WEB_VERSION_OVERRIDE),$(VERSION))
MODEL_VERSION    := $(if $(MODEL_VERSION_OVERRIDE),$(MODEL_VERSION_OVERRIDE),$(VERSION))
WAKECORE_VERSION := $(if $(WAKECORE_VERSION_OVERRIDE),$(WAKECORE_VERSION_OVERRIDE),$(VERSION))

# Determine effective suffix for each component
# Logic: 
#   - If set to NONE: no suffix
#   - If set to non-empty value: use that value
#   - If empty or undefined: inherit VERSION_SUFFIX
FSBL_EFFECTIVE_SUFFIX    := $(if $(filter NONE,$(FSBL_SUFFIX)),,$(if $(FSBL_SUFFIX),$(FSBL_SUFFIX),$(VERSION_SUFFIX)))
APP_EFFECTIVE_SUFFIX     := $(if $(filter NONE,$(APP_SUFFIX)),,$(if $(APP_SUFFIX),$(APP_SUFFIX),$(VERSION_SUFFIX)))
WEB_EFFECTIVE_SUFFIX     := $(if $(filter NONE,$(WEB_SUFFIX)),,$(if $(WEB_SUFFIX),$(WEB_SUFFIX),$(VERSION_SUFFIX)))
MODEL_EFFECTIVE_SUFFIX   := $(if $(filter NONE,$(MODEL_SUFFIX)),,$(if $(MODEL_SUFFIX),$(MODEL_SUFFIX),$(VERSION_SUFFIX)))
WAKECORE_EFFECTIVE_SUFFIX := $(if $(filter NONE,$(WAKECORE_SUFFIX)),,$(if $(WAKECORE_SUFFIX),$(WAKECORE_SUFFIX),$(VERSION_SUFFIX)))

# =============================================================================
# Version metadata
# =============================================================================
# Git information (automatically obtained during build)
GIT_COMMIT  := $(shell git rev-parse --short HEAD 2>/dev/null || echo "unknown")
GIT_BRANCH  := $(shell git rev-parse --abbrev-ref HEAD 2>/dev/null || echo "unknown")
GIT_DIRTY   := $(shell git diff --quiet 2>/dev/null || echo "-dirty")

# Build time
BUILD_DATE  := $(shell date +%Y-%m-%d)
BUILD_TIME  := $(shell date +%H:%M:%S)

# =============================================================================
# Version information print
# =============================================================================
.PHONY: version
version:
	@echo "================================================"
	@echo "NE301 Version Information"
	@echo "================================================"
	@echo "Version:      $(VERSION)"
	@echo "  MAJOR:      $(VERSION_MAJOR)"
	@echo "  MINOR:      $(VERSION_MINOR)"
	@echo "  PATCH:      $(VERSION_PATCH)"
	@echo "  BUILD:      $(VERSION_BUILD) (auto: git commits)"
	@echo ""
	@echo "Components:"
	@echo "  FSBL:       $(FSBL_VERSION)$(if $(FSBL_EFFECTIVE_SUFFIX),_$(FSBL_EFFECTIVE_SUFFIX))"
	@echo "  APP:        $(APP_VERSION)$(if $(APP_EFFECTIVE_SUFFIX),_$(APP_EFFECTIVE_SUFFIX))"
	@echo "  WEB:        $(WEB_VERSION)$(if $(WEB_EFFECTIVE_SUFFIX),_$(WEB_EFFECTIVE_SUFFIX))"
	@echo "  MODEL:      $(MODEL_VERSION)$(if $(MODEL_EFFECTIVE_SUFFIX),_$(MODEL_EFFECTIVE_SUFFIX))"
	@echo "  WAKECORE:   $(WAKECORE_VERSION)$(if $(WAKECORE_EFFECTIVE_SUFFIX),_$(WAKECORE_EFFECTIVE_SUFFIX))"
	@echo ""
	@echo "Git Info:"
	@echo "  Commit:     $(GIT_COMMIT)$(GIT_DIRTY)"
	@echo "  Branch:     $(GIT_BRANCH)"
	@echo ""
	@echo "Build Info:"
	@echo "  Date:       $(BUILD_DATE)"
	@echo "  Time:       $(BUILD_TIME)"
	@echo "================================================"
	@echo ""
	@echo "Usage:"
	@echo "  make all                    # Auto BUILD from git"
	@echo "  make all VERSION_BUILD=999  # Override BUILD number"
	@echo "================================================"

