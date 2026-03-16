#!/usr/bin/env bash

if [[ -z "${BASH_VERSION:-}" ]]; then
    echo "Error: dev.sh requires bash." >&2
    exit 1
fi

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
DATA_ROOT="${SCRIPT_DIR}/data_root"
BIN_DIR="${SCRIPT_DIR}/bin"
DRIVERS_DIR="${DATA_ROOT}/drivers"
SERVICES_DIR="${DATA_ROOT}/services"
PROJECTS_DIR="${DATA_ROOT}/projects"
PYTHON_CMD=""
PROJECT_ALIAS_WARNING_EMITTED=0
DRIVER_ALIASES=()

if [[ "${STDIOLINK_DEV_BOOTSTRAP:-0}" != "1" ]]; then
    export STDIOLINK_DEV_BOOTSTRAP=1
    exec bash --noprofile --rcfile "$0" -i
fi

shopt -s expand_aliases

prepend_path_var() {
    local var_name="$1"
    local value="$2"
    local current="${!var_name:-}"

    if [[ -n "${current}" ]]; then
        export "${var_name}=${value}:${current}"
    else
        export "${var_name}=${value}"
    fi
}

quote_command() {
    local quoted=""
    printf -v quoted '%q ' "$@"
    printf '%s' "${quoted% }"
}

find_python_cmd() {
    if command -v python3 >/dev/null 2>&1; then
        printf '%s\n' "python3"
        return 0
    fi

    if command -v python >/dev/null 2>&1; then
        printf '%s\n' "python"
        return 0
    fi

    return 1
}

command_name_from_path() {
    local path="$1"
    local name

    name="$(basename "${path}")"
    if [[ "${name}" == *.exe ]]; then
        name="${name%.exe}"
    fi
    printf '%s\n' "${name}"
}

is_driver_binary() {
    local path="$1"

    if [[ "${path}" == *.exe ]]; then
        return 0
    fi

    [[ -x "${path}" ]]
}

read_project_service_id() {
    local config_path="$1"

    if [[ -z "${PYTHON_CMD}" ]]; then
        return 1
    fi

    "${PYTHON_CMD}" - "${config_path}" <<'PY'
import json
import sys

config_path = sys.argv[1]
try:
    with open(config_path, "r", encoding="utf-8") as handle:
        data = json.load(handle)
except Exception:
    sys.exit(2)

service_id = str(data.get("serviceId") or "").strip()
if not service_id:
    sys.exit(3)

print(service_id)
PY
}

register_driver_aliases() {
    local driver_dir=""
    local exe=""
    local alias_name=""
    local alias_command=""

    if [[ ! -d "${DRIVERS_DIR}" ]]; then
        return
    fi

    while IFS= read -r -d '' driver_dir; do
        while IFS= read -r -d '' exe; do
            if ! is_driver_binary "${exe}"; then
                continue
            fi

            alias_name="$(command_name_from_path "${exe}")"
            if type -t "${alias_name}" >/dev/null 2>&1; then
                echo "WARNING: Skipping driver alias '${alias_name}': command name already exists" >&2
                continue
            fi

            alias_command="$(quote_command "${exe}")"
            alias "${alias_name}=${alias_command}"
            DRIVER_ALIASES+=("${alias_name}")
        done < <(find "${driver_dir}" -mindepth 1 -maxdepth 1 -type f -print0 | sort -z)
    done < <(find "${DRIVERS_DIR}" -mindepth 1 -maxdepth 1 -type d -print0 | sort -z)
}

register_project_aliases() {
    local project_dir=""
    local project_id=""
    local config_path=""
    local service_id=""
    local service_dir=""
    local param_path=""
    local alias_command=""

    if [[ ! -d "${PROJECTS_DIR}" ]]; then
        return
    fi

    PYTHON_CMD="$(find_python_cmd || true)"
    if [[ -z "${PYTHON_CMD}" ]]; then
        echo "WARNING: Python not found. Project aliases are unavailable in dev.sh." >&2
        PROJECT_ALIAS_WARNING_EMITTED=1
        return
    fi

    while IFS= read -r -d '' project_dir; do
        project_id="$(basename "${project_dir}")"
        config_path="${project_dir}/config.json"
        service_id=""

        if [[ -f "${config_path}" ]]; then
            if ! service_id="$(read_project_service_id "${config_path}" 2>/dev/null)"; then
                echo "WARNING: Skipping project alias '${project_id}': failed to parse ${config_path}" >&2
                continue
            fi
        else
            continue
        fi

        if [[ -z "${service_id}" ]]; then
            echo "WARNING: Skipping project alias '${project_id}': serviceId missing in ${config_path}" >&2
            continue
        fi

        if type -t "${project_id}" >/dev/null 2>&1; then
            echo "WARNING: Skipping project alias '${project_id}': command name already exists" >&2
            continue
        fi

        service_dir="${SERVICES_DIR}/${service_id}"
        param_path="${project_dir}/param.json"
        alias_command="$(quote_command stdiolink_service "${service_dir}" "--data-root=${DATA_ROOT}" "--config-file=${param_path}")"
        alias "${project_id}=${alias_command}"
    done < <(find "${PROJECTS_DIR}" -mindepth 1 -maxdepth 1 -type d -print0 | sort -z)
}

drivers() {
    echo
    echo "Available drivers:"
    echo
    if [[ "${#DRIVER_ALIASES[@]}" -gt 0 ]]; then
        printf '%s\n' "${DRIVER_ALIASES[@]}" | LC_ALL=C sort | while IFS= read -r name; do
            echo "  ${name}"
        done
    else
        echo "  (no drivers found)"
    fi
    echo
}

services() {
    local service_dir=""
    local found_any=0

    echo
    echo "Available services:"
    echo
    if [[ -d "${SERVICES_DIR}" ]]; then
        while IFS= read -r -d '' service_dir; do
            found_any=1
            echo "  $(basename "${service_dir}")"
        done < <(find "${SERVICES_DIR}" -mindepth 1 -maxdepth 1 -type d -print0 | sort -z 2>/dev/null || find "${SERVICES_DIR}" -mindepth 1 -maxdepth 1 -type d -print0)
        if [[ "${found_any}" -eq 0 ]]; then
            echo "  (no services found)"
        fi
    else
        echo "  (no services found)"
    fi
    echo
    echo "Example usage:"
    echo "  stdiolink_service \"${SERVICES_DIR}/[service-name]\" --data-root=\"${DATA_ROOT}\" --config-file=\"${PROJECTS_DIR}/[project-id]/param.json\""
    echo
}

projects() {
    local project_dir=""
    local project_id=""
    local service_label=""
    local config_path=""
    local alias_note=""
    local found_any=0

    echo
    echo "Available projects:"
    echo
    if [[ -d "${PROJECTS_DIR}" ]]; then
        while IFS= read -r -d '' project_dir; do
            found_any=1
            project_id="$(basename "${project_dir}")"
            config_path="${project_dir}/config.json"
            service_label="(service unknown)"
            alias_note=""

            if [[ -n "${PYTHON_CMD}" ]] && [[ -f "${config_path}" ]]; then
                if service_label="$(read_project_service_id "${config_path}" 2>/dev/null)"; then
                    :
                else
                    service_label="(service unknown)"
                fi
            elif [[ "${PROJECT_ALIAS_WARNING_EMITTED}" -eq 0 ]] && [[ -f "${config_path}" ]]; then
                echo "WARNING: Python not found. Project service names are unavailable in projects()." >&2
                PROJECT_ALIAS_WARNING_EMITTED=1
            fi

            if [[ "$(type -t "${project_id}" 2>/dev/null || true)" != "alias" ]]; then
                alias_note=" (alias unavailable)"
            fi

            echo "  ${project_id} -> ${service_label}${alias_note}"
        done < <(find "${PROJECTS_DIR}" -mindepth 1 -maxdepth 1 -type d -print0 | sort -z)
        if [[ "${found_any}" -eq 0 ]]; then
            echo "  (no projects found)"
        fi
    else
        echo "  (no projects found)"
    fi
    echo
    echo "To run a project:"
    echo "  [project-id]"
    echo "  [project-id] [extra args]"
    echo
}

prepend_path_var PATH "${BIN_DIR}"
prepend_path_var QT_PLUGIN_PATH "${BIN_DIR}"
prepend_path_var LD_LIBRARY_PATH "${BIN_DIR}"
prepend_path_var DYLD_LIBRARY_PATH "${BIN_DIR}"

register_driver_aliases
register_project_aliases

echo
echo "========================================"
echo "stdiolink Development Environment"
echo "========================================"
echo
echo "Environment configured:"
echo "  - bin/ added to PATH"
echo "  - Driver aliases created"
echo "  - Project aliases created"
echo
echo "To list all drivers:"
echo "  drivers"
echo
echo "To list all services:"
echo "  services"
echo
echo "To list all projects:"
echo "  projects"
echo
echo "To run a driver:"
echo "  [driver-name] --export-meta"
echo "  [driver-name] [args...]"
echo
echo "To run a project:"
echo "  [project-id]"
echo "  [project-id] [extra args]"
echo
echo "To start the server:"
echo "  stdiolink_server --data-root=\"${DATA_ROOT}\""
echo
