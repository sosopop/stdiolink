#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd -P)
ROOT_DIR=$(CDPATH= cd -- "${SCRIPT_DIR}/.." && pwd -P)
DOCKERFILE_PATH="${SCRIPT_DIR}/Dockerfile"
DOCKER_CONTEXT="${SCRIPT_DIR}"
DEFAULT_IMAGE_NAME="${STDIOLINK_DOCKER_IMAGE:-stdiolink}"
DEFAULT_TAG="${STDIOLINK_DOCKER_TAG:-latest}"
DEFAULT_CONTAINER_RELEASE_DIR="${STDIOLINK_CONTAINER_RELEASE_DIR:-/srv/stdiolink-release}"
DEFAULT_PORT="${STDIOLINK_DOCKER_PORT:-6200}"

usage() {
    cat <<'EOF'
Usage: docker/stdiolink_docker.sh <command> [options]

Commands:
  build              Build the Ubuntu 24.04 runtime image.
  run                Run a container with the release directory mounted by volume.
  stop               Stop a running container.
  rm                 Remove a container.
  logs               Show container logs.
  shell              Open an interactive shell in a running container.
  ps                 List stdiolink-related containers.
  export             Export a Docker image tar via docker save.
  import             Import a Docker image tar via docker load.
  export-container   Export a container filesystem tar via docker export.
  help               Show this help.

Notes:
  - The image does not contain stdiolink release files.
  - Use run --release-dir <path> to mount a publish directory into the container.
  - import uses docker load; export uses docker save.
EOF
}

die() {
    printf 'Error: %s\n' "$*" >&2
    exit 1
}

have_docker() {
    command -v docker >/dev/null 2>&1 || die "docker not found in PATH"
}

sanitize_name() {
    printf '%s' "$1" | tr '/: ' '---'
}

image_ref() {
    local image_name="$1"
    local image_tag="$2"
    printf '%s:%s\n' "${image_name}" "${image_tag}"
}

default_container_name() {
    local image_name="$1"
    local image_tag="$2"
    printf 'stdiolink-%s-%s\n' "$(sanitize_name "${image_name}")" "$(sanitize_name "${image_tag}")"
}

cmd_build() {
    local image_name="${DEFAULT_IMAGE_NAME}"
    local image_tag="${DEFAULT_TAG}"
    local dockerfile="${DOCKERFILE_PATH}"
    local container_release_dir="${DEFAULT_CONTAINER_RELEASE_DIR}"
    local no_cache=0

    while [[ $# -gt 0 ]]; do
        case "$1" in
            --image)
                image_name="$2"
                shift 2
                ;;
            --tag)
                image_tag="$2"
                shift 2
                ;;
            --dockerfile)
                dockerfile="$2"
                shift 2
                ;;
            --container-release-dir)
                container_release_dir="$2"
                shift 2
                ;;
            --no-cache)
                no_cache=1
                shift
                ;;
            *)
                die "unknown build option: $1"
                ;;
        esac
    done

    have_docker
    local cmd=(docker build -f "${dockerfile}" -t "$(image_ref "${image_name}" "${image_tag}")" --build-arg "STDIOLINK_RELEASE_DIR=${container_release_dir}")
    if [[ "${no_cache}" -eq 1 ]]; then
        cmd+=(--no-cache)
    fi
    cmd+=("${DOCKER_CONTEXT}")
    "${cmd[@]}"
}

cmd_run() {
    local image_name="${DEFAULT_IMAGE_NAME}"
    local image_tag="${DEFAULT_TAG}"
    local release_dir=""
    local container_name=""
    local container_release_dir="${DEFAULT_CONTAINER_RELEASE_DIR}"
    local port="${DEFAULT_PORT}"
    local detach=0
    local extra_args=()

    while [[ $# -gt 0 ]]; do
        case "$1" in
            --image)
                image_name="$2"
                shift 2
                ;;
            --tag)
                image_tag="$2"
                shift 2
                ;;
            --name)
                container_name="$2"
                shift 2
                ;;
            --release-dir)
                release_dir="$2"
                shift 2
                ;;
            --container-release-dir)
                container_release_dir="$2"
                shift 2
                ;;
            --port)
                port="$2"
                shift 2
                ;;
            --detach)
                detach=1
                shift
                ;;
            --)
                shift
                extra_args=("$@")
                break
                ;;
            *)
                die "unknown run option: $1"
                ;;
        esac
    done

    [[ -n "${release_dir}" ]] || die "run requires --release-dir"
    [[ -d "${release_dir}" ]] || die "release directory not found: ${release_dir}"
    if [[ -z "${container_name}" ]]; then
        container_name="$(default_container_name "${image_name}" "${image_tag}")"
    fi

    have_docker
    local cmd=(docker run --name "${container_name}" -p "${port}:6200" -v "${release_dir}:${container_release_dir}")
    if [[ "${detach}" -eq 1 ]]; then
        cmd+=(-d)
    else
        cmd+=(--rm -it)
    fi
    cmd+=("$(image_ref "${image_name}" "${image_tag}")")
    if [[ "${#extra_args[@]}" -gt 0 ]]; then
        cmd+=("${extra_args[@]}")
    fi
    "${cmd[@]}"
}

cmd_stop() {
    local container_name="${1:-}"
    [[ -n "${container_name}" ]] || die "stop requires a container name"
    have_docker
    docker stop "${container_name}"
}

cmd_rm() {
    local container_name="${1:-}"
    [[ -n "${container_name}" ]] || die "rm requires a container name"
    have_docker
    docker rm -f "${container_name}"
}

cmd_logs() {
    local follow=0
    local container_name=""
    while [[ $# -gt 0 ]]; do
        case "$1" in
            -f|--follow)
                follow=1
                shift
                ;;
            *)
                container_name="$1"
                shift
                ;;
        esac
    done
    [[ -n "${container_name}" ]] || die "logs requires a container name"
    have_docker
    if [[ "${follow}" -eq 1 ]]; then
        docker logs -f "${container_name}"
    else
        docker logs "${container_name}"
    fi
}

cmd_shell() {
    local container_name="${1:-}"
    [[ -n "${container_name}" ]] || die "shell requires a running container name"
    have_docker
    docker exec -it "${container_name}" bash
}

cmd_ps() {
    have_docker
    docker ps -a --filter "name=stdiolink"
}

cmd_export() {
    local image_name="${DEFAULT_IMAGE_NAME}"
    local image_tag="${DEFAULT_TAG}"
    local output_path=""
    while [[ $# -gt 0 ]]; do
        case "$1" in
            --image)
                image_name="$2"
                shift 2
                ;;
            --tag)
                image_tag="$2"
                shift 2
                ;;
            --output)
                output_path="$2"
                shift 2
                ;;
            *)
                die "unknown export option: $1"
                ;;
        esac
    done
    [[ -n "${output_path}" ]] || die "export requires --output"
    have_docker
    mkdir -p "$(dirname -- "${output_path}")"
    docker save -o "${output_path}" "$(image_ref "${image_name}" "${image_tag}")"
}

cmd_import() {
    local input_path="${1:-}"
    [[ -n "${input_path}" ]] || die "import requires an input tar path"
    [[ -f "${input_path}" ]] || die "input tar not found: ${input_path}"
    have_docker
    docker load -i "${input_path}"
}

cmd_export_container() {
    local container_name=""
    local output_path=""
    while [[ $# -gt 0 ]]; do
        case "$1" in
            --name)
                container_name="$2"
                shift 2
                ;;
            --output)
                output_path="$2"
                shift 2
                ;;
            *)
                die "unknown export-container option: $1"
                ;;
        esac
    done
    [[ -n "${container_name}" ]] || die "export-container requires --name"
    [[ -n "${output_path}" ]] || die "export-container requires --output"
    have_docker
    mkdir -p "$(dirname -- "${output_path}")"
    docker export -o "${output_path}" "${container_name}"
}

main() {
    local command="${1:-help}"
    shift || true

    case "${command}" in
        build)
            cmd_build "$@"
            ;;
        run)
            cmd_run "$@"
            ;;
        stop)
            cmd_stop "$@"
            ;;
        rm)
            cmd_rm "$@"
            ;;
        logs)
            cmd_logs "$@"
            ;;
        shell)
            cmd_shell "$@"
            ;;
        ps)
            cmd_ps
            ;;
        export)
            cmd_export "$@"
            ;;
        import|import-image)
            cmd_import "$@"
            ;;
        export-container)
            cmd_export_container "$@"
            ;;
        help|-h|--help)
            usage
            ;;
        *)
            die "unknown command: ${command}"
            ;;
    esac
}

main "$@"
