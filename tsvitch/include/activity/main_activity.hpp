

#pragma once

#include <borealis/core/activity.hpp>
#include <borealis/core/bind.hpp>

class AutoTabFrame;

class MainActivity : public brls::Activity {
public:
    MainActivity();

    CONTENT_FROM_XML_RES("activity/main.xml");

    void onContentAvailable() override;

    ~MainActivity() override;
    

private:
    BRLS_BIND(AutoTabFrame, tabFrame, "main/tabFrame");
};
