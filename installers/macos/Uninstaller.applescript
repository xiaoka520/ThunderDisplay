on run
    set useChinese to false
    try
        set languages to do shell script "/usr/bin/defaults read -g AppleLanguages"
        set useChinese to (languages contains "zh-Hans") or (languages contains "zh-Hant") or (languages contains "zh-CN")
    end try
    if useChinese then
        set promptText to "卸载 ThunderDisplay？应用、自启动和登录前组件会被移除，连接偏好与已授予的权限会保留。"
        set cancelText to "取消"
        set uninstallText to "卸载"
        set doneText to "ThunderDisplay 已卸载。"
    else
        set promptText to "Uninstall ThunderDisplay? Removes the apps and startup components, and keeps connection preferences and permission grants."
        set cancelText to "Cancel"
        set uninstallText to "Uninstall"
        set doneText to "ThunderDisplay has been uninstalled."
    end if
    try
        display dialog promptText buttons {cancelText, uninstallText} default button uninstallText cancel button cancelText with icon caution with title "ThunderDisplay"
        do shell script "/bin/bash '/Applications/ThunderDisplayHost.app/Contents/Resources/uninstall-app.sh'" with administrator privileges
        display dialog doneText buttons {"OK"} default button "OK" with title "ThunderDisplay"
    on error messageText number errorNumber
        if errorNumber is not -128 then display dialog messageText buttons {"OK"} default button "OK" with icon stop with title "ThunderDisplay"
    end try
end run
