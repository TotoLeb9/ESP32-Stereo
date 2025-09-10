#pragma once
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"
#include <string>
#include <memory>

class Memory{
    public:
        Memory(std::string ns_name);
        ~Memory(){
            if(is_open) close();
        }
        void init_nvs();
        esp_err_t open(nvs_open_mode_t open_mode);
        void close();

        template<typename T>
        esp_err_t nvs_set(const std::string& key, const T& value) {
            if constexpr (sizeof(T)==1){
                if constexpr (std::is_same<T, uint8_t>::value) {
                    return nvs_set_u8(nvs_handle, key.c_str(), value);
                } else if constexpr (std::is_same<T, int8_t>::value) {
                    return nvs_set_i8(nvs_handle, key.c_str(), value);
                }
            } else if constexpr (sizeof(T)==2) {
                if constexpr (std::is_same<T, uint16_t>::value) {
                    return nvs_set_u16(nvs_handle, key.c_str(), value);
                } else if constexpr (std::is_same<T, int16_t>::value) {
                    return nvs_set_i16(nvs_handle, key.c_str(), value);
                }
            } else if constexpr (sizeof(T)==4) {
                if constexpr (std::is_same<T, uint32_t>::value) {   
                return nvs_set_u32(nvs_handle, key.c_str(), value);
                } else if constexpr (std::is_same<T, int32_t>::value) {
                    return nvs_set_i32(nvs_handle, key.c_str(), value);
                } else if constexpr (std::is_same<T, float>::value) {
                    return nvs_set_f32(nvs_handle, key.c_str(), value);
                }
            } else if constexpr (sizeof(T)==8) {
                if constexpr (std::is_same<T, uint64_t>::value) {
                    return nvs_set_u64(nvs_handle, key.c_str(), value);
                } else if constexpr (std::is_same<T, int64_t>::value) {
                    return nvs_set_i64(nvs_handle, key.c_str(), value);
                }
            } else if constexpr (std::is_same<T, std::string>::value) {
                return nvs_set_str(nvs_handle, key.c_str(), value.c_str()); 
            } else {
                ESP_LOGE(TAG, "Type non supporté pour la clé %s", key.c_str());
                return ESP_ERR_NVS_INVALID_NAME;
        }
            return ESP_OK;
        }

        template<typename T>
        esp_err_t nvs_get(const std::string& key, T& value) {
            if constexpr (sizeof(T)==1){
                if constexpr (std::is_same<T, uint8_t>::value) {
                    return nvs_get_u8(nvs_handle, key.c_str(), &value);
                } else if constexpr (std::is_same<T, int8_t>::value) {
                    return nvs_get_i8(nvs_handle, key.c_str(), &value);
                }
            } else if constexpr (sizeof(T)==2) {
                if constexpr (std::is_same<T, uint16_t>::value) {
                    return nvs_get_u16(nvs_handle, key.c_str(), &value);
                } else if constexpr (std::is_same<T, int16_t>::value) {
                    return nvs_get_i16(nvs_handle, key.c_str(), &value);
                }
            } else if constexpr (sizeof(T)==4) {
                if constexpr (std::is_same<T, uint32_t>::value) {   
                    return nvs_get_u32(nvs_handle, key.c_str(), &value);
                } else if constexpr (std::is_same<T, int32_t>::value) {
                    return nvs_get_i32(nvs_handle, key.c_str(), &value);
                } else if constexpr (std::is_same<T, float>::value) {
                    return nvs_get_f32(nvs_handle, key.c_str(), &value);
                }
            } else if constexpr (sizeof(T)==8) {
                if constexpr (std::is_same<T, uint64_t>::value) {
                    return nvs_get_u64(nvs_handle, key.c_str(), &value);
                } else if constexpr (std::is_same<T, int64_t>::value) {
                    return nvs_get_i64(nvs_handle, key.c_str(), &value);
                }
            } else if constexpr (std::is_same<T, std::string>::value) {
                size_t required_size = 0;
                esp_err_t err = nvs_get_str(nvs_handle, key.c_str(), nullptr, &required_size);
                if (err != ESP_OK) return err;
                std::string tmp(required_size, '\0');
                err = nvs_get_str(nvs_handle, key.c_str(), tmp.data(), &required_size);
                if (err == ESP_OK) value = tmp.c_str();
                return err;
            } else {
                ESP_LOGE(TAG, "Type non supporté pour la clé %s", key.c_str());
                return ESP_ERR_NVS_INVALID_NAME;
            }
            return ESP_OK;
        }

    private:
        nvs_handle_t nvs_handle;
        std::string namespace_name;
        bool is_open = false;
        nvs_open_mode_t current_mode;
        const char* TAG;

};

