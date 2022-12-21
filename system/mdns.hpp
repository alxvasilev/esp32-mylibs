#ifndef _MDNS_H_INCLUDED
#define _MDNS_H_INCLUDED
#include <mdns.h>
class MDns {
public:
    esp_err_t start(const char* hostName, const  char* instName=nullptr)
    {
        //initialize mDNS service
        esp_err_t err = mdns_init();
        if (err) {
            return err;
        }
        //set hostname
        mdns_hostname_set(hostName);
        //set default instance
        if (instName) {
            mdns_instance_name_set(instName);
        }
        return ESP_OK;
    }
};
#endif
