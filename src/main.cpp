#include <zephyr/logging/log.h>
#include "respmon/pipeline.hpp"

LOG_MODULE_DECLARE(respmon);

int main()
{
    LOG_INF("Respiratory monitor starting (simulated educational data)");
    respmon::start_pipeline();
    return 0;
}
