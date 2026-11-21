# Magisk install-time checks for the onn 4K Pro Dolby Vision FEL module.
FW=$(getprop ro.build.version.incremental)
case "$FW" in
  15016191) ui_print "- ONN 4K Pro v2, Canadian firmware $FW";;
  15640252) ui_print "- ONN 4K Pro v2, US firmware $FW";;
  *) abort "! Unsupported firmware $FW. This module only supports the ONN 4K Pro v2 builds 15016191 and 15640252.";;
esac
set_perm_recursive $MODPATH 0 0 0755 0644
set_perm $MODPATH/service.sh 0 0 0755
set_perm $MODPATH/dvfelctl 0 0 0755
set_perm $MODPATH/lldv_setup.sh 0 0 0755
ui_print "- Settings: open the Dolby Vision FEL app (no root grant needed)"
