#include "include/memory.hpp"

Memory::Memory(std::string ns_name) : nvs_handle(0),
namespace_name(ns_name) , 
is_open(false) , current_mode(NVS_READWRITE) , TAG("Memory") {
    if(ns_name.empty() || ns_name.length()>NVS_KEY_NAME_MAX_SIZE){
        ESP_LOGE(TAG,"Invalid namespace name %s" , ns_name.c_str());
        //throw std::invalid_argument("Invalid namespace name");
    }
}

esp_err_t Memory::open(nvs_open_mode_t open_mode = NVS_READWRITE){
    if(is_open){
        if(current_mode==open_mode){
            ESP_LOGW(TAG, "NVS is already open in the requested mode");
            return ESP_OK;
        } else {
            close();
            ESP_LOGI(TAG, "Reopening NVS in a different mode");}
    }
    esp_err_t err = nvs_open(namespace_name.c_str(), open_mode, &nvs_handle);
    if(err != ESP_OK){
        ESP_LOGE(TAG, "Failed to open NVS namespace %s: %s", namespace_name.c_str(), esp_err_to_name(err));
        return err;
    if(err==ESP_OK){
        is_open = true;
        current_mode = open_mode;
        ESP_LOGI(TAG, "NVS namespace %s opened successfully in mode %d", namespace_name.c_str(), open_mode);
        return ESP_OK;
    }
    else{
        is_open = false;
        //throw std::runtime_error("Failed to open NVS namespace");
        return err;
    }

}
return err;
}

void Memory::close(){
    if(is_open){
        nvs_close(nvs_handle);
        is_open = false;
        nvs_handle = 0; // Reset handle to avoid dangling pointer
        ESP_LOGI(TAG, "NVS namespace %s closed successfully", namespace_name.c_str());
    } else {
        ESP_LOGW(TAG, "NVS namespace %s is not open", namespace_name.c_str());
    }
}

 void Memory::init_nvs(){
    esp_err_t ret = nvs_flash_init();
    if(ret==ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND){
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    ESP_LOGI("NVS" , "NVS initialisé avec succès");
 }