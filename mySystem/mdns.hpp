#ifndef _MDNS_H_INCLUDED
#define _MDNS_H_INCLUDED
#include <mdns.h>
class MDns {
public:
    struct TxtItem: public mdns_txt_item_t {
        TxtItem(const char* aKey, const char* aVal)
        {
            key = aKey; value = aVal;
        }
    };
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
    void registerService(const char* name, const char* type, const char* proto, int port,
        const std::vector<TxtItem>& txtItems)
    {
        mdns_service_add(name, type, proto, port, (mdns_txt_item_t*)txtItems.data(), txtItems.size());
    }

};
#endif
