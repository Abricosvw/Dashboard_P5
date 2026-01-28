# ES8311 component disabled - audio_manager.c handles ES8311 directly
# using new i2c_master driver to avoid conflict with legacy i2c driver
idf_component_register()
