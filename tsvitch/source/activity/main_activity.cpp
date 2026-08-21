/*
    Copyright 2020-2021 natinusala

    Licensed under the Apache License, Version 2.0 (the "License");
    you may not use this file except in compliance with the License.
    You may obtain a copy of the License at

        http://www.apache.org/licenses/LICENSE-2.0

    Unless required by applicable law or agreed to in writing, software
    distributed under the License is distributed on an "AS IS" BASIS,
    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
    See the License for the specific language governing permissions and
    limitations under the License.
*/

#include "activity/main_activity.hpp"
#include "view/auto_tab_frame.hpp"

MainActivity::MainActivity() {
    brls::Logger::info("MainActivity constructor called");
}

MainActivity::~MainActivity() { brls::Logger::debug("del MainActivity"); }

void MainActivity::onContentAvailable() {
    brls::Logger::info("MainActivity::onContentAvailable() called");

    if (this->tabFrame && this->tabFrame->getSidebar()) {
        this->tabFrame->getSidebar()->setVisibility(brls::Visibility::GONE);
        this->tabFrame->getSidebar()->setWidth(0);
        this->tabFrame->getSidebar()->setFocusable(false);
        for (auto* child : this->tabFrame->getSidebar()->getChildren()) {
            child->setVisibility(brls::Visibility::GONE);
            child->setFocusable(false);
        }
    }
    
    // Controlla la connettività internet
    bool hasInternet = brls::Application::getPlatform()->hasEthernetConnection() || 
                      brls::Application::getPlatform()->hasWirelessConnection();
    
    if (!hasInternet) {
        brls::Logger::info("No internet connection detected, navigating to Downloads tab");
        // Se non c'è internet, vai direttamente al tab Downloads (indice 2)
        if (this->tabFrame) {
            this->tabFrame->focusTab(2); // Assumendo che Downloads sia il 3° tab (indice 2)
        }
    }
}
