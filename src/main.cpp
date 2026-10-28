#include <zephyr/sys/printk.h>
#include "respmon/pipeline.hpp"

int main()
{
    printk("Respiratory monitor starting (simulated educational data)\n");
    respmon::start_pipeline();
    return 0;
}
