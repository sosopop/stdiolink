#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd -P)
SCRIPT_NAME=$(basename -- "$0")
DEFAULT_IMAGE_NAME="${STDIOLINK_DOCKER_IMAGE:-stdiolink}"
DEFAULT_TAG="${STDIOLINK_DOCKER_TAG:-latest}"
DEFAULT_CONTAINER_RELEASE_DIR="${STDIOLINK_CONTAINER_RELEASE_DIR:-/srv/stdiolink-release}"
DEFAULT_PORT="${STDIOLINK_DOCKER_PORT:-6200}"
BUNDLE_ENV_NAME="DOCKER_BUNDLE.env"

usage() {
    cat <<'EOF'
Usage: stdiolink_docker.sh <command> [options]

Commands:
  build              Build the Ubuntu 24.04 runtime image.
  run                Run the bundle release in Docker. In a bundle root, no arguments auto-load the image and run in background.
  stop               Stop the default or specified container.
  rm                 Remove the default or specified container.
  logs               Show logs for the default or specified container.
  shell              Open a shell in the default or specified running container.
  ps                 List stdiolink-related containers.
  export             Export a Docker image tar via docker save.
  import             Import a Docker image tar via docker load. In a bundle root, no arguments auto-import the bundled image tar.
  export-container   Export a container filesystem tar via docker export.
  help               Show this help.

Bundle Mode:
  When this script lives in a Docker bundle root alongside DOCKER_BUNDLE.env, release/, and *_docker_image.tar,
  zero-argument commands auto-discover the release directory, image tar, image tag, and container name.
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

resolve_docker_assets_dir() {
    if [[ -f "${SCRIPT_DIR}/Dockerfile" ]]; then
        printf '%s\n' "${SCRIPT_DIR}"
        return 0
    fi
    if [[ -f "${SCRIPT_DIR}/docker/Dockerfile" ]]; then
        printf '%s\n' "${SCRIPT_DIR}/docker"
        return 0
    fi
    die "Dockerfile not found next to ${SCRIPT_NAME} or in ${SCRIPT_DIR}/docker"
}

bundle_env_path() {
    printf '%s\n' "${SCRIPT_DIR}/${BUNDLE_ENV_NAME}"
}

fallback_bundle_package_name() {
    local base_name
    base_name=$(basename -- "${SCRIPT_DIR}")
    if [[ "${base_name}" == *_docker_bundle ]]; then
        printf '%s\n' "${base_name%_docker_bundle}"
        return 0
    fi
    return 1
}

find_single_match() {
    local mode="$1"
    local pattern="$2"
    local matches=()
    shopt -s nullglob
    if [[ "${mode}" == "file" ]]; then
        matches=(${pattern})
    else
        matches=(${pattern})
    fi
    shopt -u nullglob
    if [[ "${#matches[@]}" -eq 1 ]]; then
        printf '%s\n' "${matches[0]}"
        return 0
    fi
    return 1
}

is_bundle_root() {
    [[ -f "$(bundle_env_path)" ]] && return 0
    [[ -d "${SCRIPT_DIR}/release" ]] || return 1
    find_single_match "file" "${SCRIPT_DIR}"/*_docker_image.tar >/dev/null 2>&1 || return 1
    find_single_match "dir" "${SCRIPT_DIR}"/release/* >/dev/null 2>&1 || return 1
    return 0
}

load_bundle_env() {
    local env_path
    env_path="$(bundle_env_path)"
    [[ -f "${env_path}" ]] || die "bundle metadata not found: ${env_path}"
    # shellcheck disable=SC1090
    . "${env_path}"
}

bundle_image_name() {
    if is_bundle_root; then
        if [[ -f "$(bundle_env_path)" ]]; then
            load_bundle_env
            printf '%s\n' "${BUNDLE_IMAGE_NAME}"
        else
            printf '%s\n' "${DEFAULT_IMAGE_NAME}"
        fi
        return 0
    fi
    printf '%s\n' "${DEFAULT_IMAGE_NAME}"
}

bundle_image_tag() {
    if is_bundle_root; then
        if [[ -f "$(bundle_env_path)" ]]; then
            load_bundle_env
            printf '%s\n' "${BUNDLE_IMAGE_TAG}"
        else
            printf '%s\n' "$(fallback_bundle_package_name || printf '%s' "${DEFAULT_TAG}")"
        fi
        return 0
    fi
    printf '%s\n' "${DEFAULT_TAG}"
}

bundle_container_release_dir() {
    if is_bundle_root; then
        if [[ -f "$(bundle_env_path)" ]]; then
            load_bundle_env
            printf '%s\n' "${BUNDLE_CONTAINER_RELEASE_DIR}"
        else
            printf '%s\n' "${DEFAULT_CONTAINER_RELEASE_DIR}"
        fi
        return 0
    fi
    printf '%s\n' "${DEFAULT_CONTAINER_RELEASE_DIR}"
}

bundle_port() {
    if is_bundle_root; then
        if [[ -f "$(bundle_env_path)" ]]; then
            load_bundle_env
            printf '%s\n' "${BUNDLE_PORT}"
        else
            printf '%s\n' "${DEFAULT_PORT}"
        fi
        return 0
    fi
    printf '%s\n' "${DEFAULT_PORT}"
}

bundle_release_dir() {
    if [[ -f "$(bundle_env_path)" ]]; then
        load_bundle_env
        printf '%s\n' "${SCRIPT_DIR}/${BUNDLE_RELEASE_DIR}"
        return 0
    fi
    find_single_match "dir" "${SCRIPT_DIR}"/release/* || die "bundle release directory not found under ${SCRIPT_DIR}/release"
}

bundle_image_tar_path() {
    if [[ -f "$(bundle_env_path)" ]]; then
        load_bundle_env
        printf '%s\n' "${SCRIPT_DIR}/${BUNDLE_IMAGE_TAR}"
        return 0
    fi
    find_single_match "file" "${SCRIPT_DIR}"/*_docker_image.tar || die "bundle docker image tar not found in ${SCRIPT_DIR}"
}

bundle_container_name() {
    if is_bundle_root; then
        if [[ -f "$(bundle_env_path)" ]]; then
            load_bundle_env
            printf '%s\n' "${BUNDLE_CONTAINER_NAME}"
        else
            printf '%s\n' "$(default_container_name "$(bundle_image_name)" "$(bundle_image_tag)")"
        fi
        return 0
    fi
    printf '%s\n' "$(default_container_name "$(bundle_image_name)" "$(bundle_image_tag)")"
}

docker_image_exists() {
    local image="$1"
    docker image inspect "${image}" >/dev/null 2>&1
}

docker_container_exists() {
    local container_name="$1"
    docker container inspect "${container_name}" >/dev/null 2>&1
}

docker_container_running() {
    local container_name="$1"
    [[ "$(docker inspect -f '{{.State.Running}}' "${container_name}" 2>/dev/null || true)" == "true" ]]
}

ensure_bundle_image_loaded() {
    local image_name="$1"
    local image_tag="$2"
    local image_tar="${3:-}"
    local ref
    ref="$(image_ref "${image_name}" "${image_tag}")"
    if docker_image_exists "${ref}"; then
        return 0
    fi
    [[ -n "${image_tar}" ]] || die "docker image ${ref} not found and no image tar provided"
    [[ -f "${image_tar}" ]] || die "docker image tar not found: ${image_tar}"
    printf 'Loading Docker image: %s\n' "${image_tar}"
    docker load -i "${image_tar}"
}

cmd_build() {
    local image_name="${DEFAULT_IMAGE_NAME}"
    local image_tag="${DEFAULT_TAG}"
    local docker_dir
    docker_dir="$(resolve_docker_assets_dir)"
    local dockerfile="${docker_dir}/Dockerfile"
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
    cmd+=("${docker_dir}")
    "${cmd[@]}"
}

cmd_run() {
    local image_name=""
    local image_tag=""
    local release_dir=""
    local container_name=""
    local container_release_dir=""
    local port=""
    local image_tar=""
    local detach=0
    local foreground=0
    local explicit_args=0
    local extra_args=()

    while [[ $# -gt 0 ]]; do
        case "$1" in
            --image)
                image_name="$2"
                explicit_args=1
                shift 2
                ;;
            --tag)
                image_tag="$2"
                explicit_args=1
                shift 2
                ;;
            --name)
                container_name="$2"
                explicit_args=1
                shift 2
                ;;
            --release-dir)
                release_dir="$2"
                explicit_args=1
                shift 2
                ;;
            --container-release-dir)
                container_release_dir="$2"
                explicit_args=1
                shift 2
                ;;
            --port)
                port="$2"
                explicit_args=1
                shift 2
                ;;
            --image-tar)
                image_tar="$2"
                explicit_args=1
                shift 2
                ;;
            --detach)
                detach=1
                explicit_args=1
                shift
                ;;
            --foreground)
                foreground=1
                explicit_args=1
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

    if is_bundle_root; then
        [[ -n "${image_name}" ]] || image_name="$(bundle_image_name)"
        [[ -n "${image_tag}" ]] || image_tag="$(bundle_image_tag)"
        [[ -n "${release_dir}" ]] || release_dir="$(bundle_release_dir)"
        [[ -n "${container_release_dir}" ]] || container_release_dir="$(bundle_container_release_dir)"
        [[ -n "${port}" ]] || port="$(bundle_port)"
        [[ -n "${image_tar}" ]] || image_tar="$(bundle_image_tar_path)"
        [[ -n "${container_name}" ]] || container_name="$(bundle_container_name)"
        if [[ "${explicit_args}" -eq 0 ]]; then
            detach=1
        fi
    else
        [[ -n "${image_name}" ]] || image_name="${DEFAULT_IMAGE_NAME}"
        [[ -n "${image_tag}" ]] || image_tag="${DEFAULT_TAG}"
        [[ -n "${container_release_dir}" ]] || container_release_dir="${DEFAULT_CONTAINER_RELEASE_DIR}"
        [[ -n "${port}" ]] || port="${DEFAULT_PORT}"
        [[ -n "${container_name}" ]] || container_name="$(default_container_name "${image_name}" "${image_tag}")"
    fi

    [[ -n "${release_dir}" ]] || die "run requires --release-dir unless executed from a Docker bundle root"
    [[ -d "${release_dir}" ]] || die "release directory not found: ${release_dir}"

    have_docker
    ensure_bundle_image_loaded "${image_name}" "${image_tag}" "${image_tar}"
    local ref
    ref="$(image_ref "${image_name}" "${image_tag}")"

    if docker_container_exists "${container_name}"; then
        if docker_container_running "${container_name}"; then
            printf 'Container already running: %s\n' "${container_name}"
            return 0
        fi
        if [[ "${#extra_args[@]}" -gt 0 ]] || [[ "${foreground}" -eq 1 ]]; then
            die "existing stopped container '${container_name}' cannot be restarted with extra runtime arguments"
        fi
        printf 'Starting existing container: %s\n' "${container_name}"
        docker start "${container_name}" >/dev/null
        return 0
    fi

    local cmd=(docker run --name "${container_name}" -p "${port}:6200" -v "${release_dir}:${container_release_dir}")
    if [[ "${detach}" -eq 1 ]] && [[ "${foreground}" -eq 0 ]]; then
        cmd+=(-d --restart unless-stopped)
    else
        cmd+=(--rm -it)
    fi
    cmd+=("${ref}")
    if [[ "${#extra_args[@]}" -gt 0 ]]; then
        cmd+=("${extra_args[@]}")
    fi
    "${cmd[@]}"
}

resolve_target_container() {
    local requested="${1:-}"
    if [[ -n "${requested}" ]]; then
        printf '%s\n' "${requested}"
        return 0
    fi
    if is_bundle_root; then
        bundle_container_name
        return 0
    fi
    die "container name required outside a Docker bundle root"
}

cmd_stop() {
    local container_name
    container_name="$(resolve_target_container "${1:-}")"
    have_docker
    docker stop "${container_name}"
}

cmd_rm() {
    local container_name
    container_name="$(resolve_target_container "${1:-}")"
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
    container_name="$(resolve_target_container "${container_name}")"
    have_docker
    if [[ "${follow}" -eq 1 ]]; then
        docker logs -f "${container_name}"
    else
        docker logs "${container_name}"
    fi
}

cmd_shell() {
    local container_name
    container_name="$(resolve_target_container "${1:-}")"
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
    if [[ -z "${input_path}" ]] && is_bundle_root; then
        input_path="$(bundle_image_tar_path)"
    fi
    [[ -n "${input_path}" ]] || die "import requires an input tar path unless executed from a Docker bundle root"
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
    [[ -n "${output_path}" ]] || die "export-container requires --output"
    container_name="$(resolve_target_container "${container_name}")"
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
