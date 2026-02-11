#!/usr/bin/env bash
set -euo pipefail

BASE_URL="${1:-http://127.0.0.1:18080}"
CURL_COMMON_ARGS=(
    --noproxy "*"
    -sS
    --connect-timeout 3
    --max-time 20
)

call_json() {
    local method="$1"
    local path="$2"
    local body="${3:-}"

    if [[ -n "${body}" ]]; then
        curl "${CURL_COMMON_ARGS[@]}" -X "${method}" "${BASE_URL}${path}" \
            -H "Content-Type: application/json" \
            -d "${body}"
    else
        curl "${CURL_COMMON_ARGS[@]}" -X "${method}" "${BASE_URL}${path}"
    fi
}

show_step() {
    local text="$1"
    printf '\n=== %s ===\n' "${text}"
}

extract_instance_id() {
    sed -n 's/.*"instanceId":"\([^"]*\)".*/\1/p'
}

show_step "GET /api/services"
resp="$(call_json GET /api/services)"
echo "${resp}"
echo "${resp}" | grep -q '"quick_start_service"'

show_step "GET /api/services/quick_start_service"
call_json GET /api/services/quick_start_service

show_step "GET /api/projects"
resp="$(call_json GET /api/projects)"
echo "${resp}"
echo "${resp}" | grep -q '"manual_demo"'

show_step "GET /api/projects/manual_demo"
call_json GET /api/projects/manual_demo

show_step "POST /api/projects/manual_demo/validate (valid)"
call_json POST /api/projects/manual_demo/validate \
    '{"config":{"mode":"normal","runMs":150,"message":"validate ok"}}'

show_step "POST /api/projects/manual_demo/validate (invalid)"
call_json POST /api/projects/manual_demo/validate \
    '{"config":{"mode":"normal","runMs":"bad-value","message":"validate fail"}}'

show_step "POST /api/projects/manual_demo/start"
manual_start="$(call_json POST /api/projects/manual_demo/start)"
echo "${manual_start}"

show_step "GET /api/instances?projectId=manual_demo"
call_json GET "/api/instances?projectId=manual_demo"

show_step "POST /api/projects/fixed_rate_demo/start"
fixed_start="$(call_json POST /api/projects/fixed_rate_demo/start)"
echo "${fixed_start}"
fixed_instance_id="$(echo "${fixed_start}" | extract_instance_id)"

if [[ -n "${fixed_instance_id}" ]]; then
    show_step "POST /api/instances/${fixed_instance_id}/terminate"
    call_json POST "/api/instances/${fixed_instance_id}/terminate"
else
    echo "Skip terminate: no fixed_rate instanceId found"
fi

sleep 1

show_step "GET /api/instances/manual_demo/logs?lines=20"
call_json GET "/api/instances/manual_demo/logs?lines=20"

show_step "POST /api/projects/manual_demo/reload"
call_json POST /api/projects/manual_demo/reload

show_step "PUT /api/projects/manual_demo"
call_json PUT /api/projects/manual_demo \
    '{"id":"manual_demo","name":"Manual Demo Updated","serviceId":"quick_start_service","enabled":true,"schedule":{"type":"manual"},"config":{"mode":"normal","runMs":120,"message":"updated by api_smoke"}}'

show_step "POST /api/projects/temp_api_demo"
call_json POST /api/projects \
    '{"id":"temp_api_demo","name":"Temp API Demo","serviceId":"quick_start_service","enabled":true,"schedule":{"type":"manual"},"config":{"mode":"normal","runMs":100,"message":"temp create"}}'

show_step "GET /api/projects/temp_api_demo"
call_json GET /api/projects/temp_api_demo

show_step "DELETE /api/projects/temp_api_demo"
call_json DELETE /api/projects/temp_api_demo

show_step "POST /api/projects/fixed_rate_demo/stop"
call_json POST /api/projects/fixed_rate_demo/stop

show_step "GET /api/drivers"
call_json GET /api/drivers

show_step "POST /api/drivers/scan"
call_json POST /api/drivers/scan '{"refreshMeta":true}'

show_step "Smoke done"
echo "All major API groups were exercised."
