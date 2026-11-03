juce_add_console_app(MultitailsTests PRODUCT_NAME "MultitailsTests")
target_sources(MultitailsTests PRIVATE TestMain.cpp ${PROJECT_SOURCE_DIR}/Source/TailsProcessor.cpp)
target_include_directories(MultitailsTests PRIVATE ${PROJECT_SOURCE_DIR}/Source)
target_compile_definitions(MultitailsTests PRIVATE
    MULTITAILS_NO_PLUGIN_ENTRY=1 JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_USE_FLAC=1
    JucePlugin_Name="MultitailsTests" JUCE_STANDALONE_APPLICATION=1)
target_link_libraries(MultitailsTests PRIVATE
    MultitailsFonts MultitailsIrs
    juce::juce_audio_processors juce::juce_audio_formats juce::juce_gui_basics juce::juce_audio_basics
    juce::juce_recommended_config_flags)
