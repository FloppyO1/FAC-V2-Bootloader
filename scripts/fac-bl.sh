#!/bin/sh
# Docker wrapper for the F.A.C. V2 bootloader: build, host tests, shell, clean.
# Works on Linux, macOS and Git Bash (Windows).
set -e

IMAGE="fac-bootloader-build:local"
SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$SCRIPT_DIR/.." && pwd)

# Git Bash: stop path rewriting and give Docker the Windows path
export MSYS_NO_PATHCONV=1
MOUNT="$ROOT"
if (cd "$ROOT" && pwd -W) >/dev/null 2>&1; then
	MOUNT=$(cd "$ROOT" && pwd -W)
fi

# On Linux run as the current user so build files are not owned by root
USER_ARGS=""
if [ "$(uname -s)" = "Linux" ]; then
	USER_ARGS="--user $(id -u):$(id -g)"
fi

usage() {
	cat <<USAGE
Usage: $0 <command>
  image                    build the Docker image ($IMAGE)
  build [Debug|Release]    build the firmware (default Release)
  test                     build and run the host unit tests
  shell                    interactive bash in the container
  clean                    remove build/ and tests/build
USAGE
}

ensure_image() {
	if ! docker image inspect "$IMAGE" >/dev/null 2>&1; then
		build_image
	fi
}

build_image() {
	docker build -t "$IMAGE" -f "$ROOT/docker/Dockerfile" "$ROOT/docker"
}

# run_in_container <interactive flags> <command...>
run_in_container() {
	flags="$1"
	shift
	# shellcheck disable=SC2086
	docker run --rm $flags $USER_ARGS -v "$MOUNT:/work" -w /work "$IMAGE" "$@"
}

CMD="${1:-}"
case "$CMD" in
	image)
		build_image
		;;
	build)
		PRESET="${2:-Release}"
		case "$PRESET" in
			Debug|Release) ;;
			*) echo "Unknown build type: $PRESET (use Debug or Release)" >&2; exit 1 ;;
		esac
		ensure_image
		run_in_container "" sh -c "cmake --preset $PRESET && cmake --build build/$PRESET"
		;;
	test)
		ensure_image
		run_in_container "" sh -c "cmake -S tests -B tests/build -G Ninja && cmake --build tests/build && ctest --test-dir tests/build --output-on-failure"
		;;
	shell)
		ensure_image
		run_in_container "-it" bash
		;;
	clean)
		ensure_image
		run_in_container "" sh -c "rm -rf build tests/build"
		;;
	*)
		usage
		[ -n "$CMD" ] && exit 1
		exit 0
		;;
esac
