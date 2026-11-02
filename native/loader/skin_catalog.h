// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
#ifndef SKINLAB_SKIN_CATALOG_H
#define SKINLAB_SKIN_CATALOG_H
#include "skin_catalog_generated.h"
inline const SlCatalogModel* sl_catalog_model(const char* model) {
    for(const auto& entry:sl_catalog_models)if(!lstrcmpiA(entry.name,model))return &entry;
    return nullptr;
}
inline bool sl_catalog_skin(const char* model,int32_t skin) {
    const auto* entry=sl_catalog_model(model);
    if(entry)for(uint32_t i=0;i<entry->count;++i)if(sl_catalog_numbers[entry->first+i]==skin)return true;
    return false;
}
#endif
