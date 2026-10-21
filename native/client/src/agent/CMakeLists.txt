suitecut_sdk_module(agent agent.cpp agent_driver.cpp simple_agent.cpp agent_tools.cpp)
target_link_libraries(suitecut_sdk_agent PRIVATE suitecut_sdk_browser Threads::Threads)
