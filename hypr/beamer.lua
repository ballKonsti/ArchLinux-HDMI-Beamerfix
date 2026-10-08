-- beamer: automatic HDMI / projector handling (~/Projects/hdmi-beamer)
hl.on("hyprland.start", function()
    hl.exec_cmd("beamer daemon")
end)

-- SUPER+P: cycle mirror -> extend -> external only -> laptop only
hl.bind("SUPER + P",         hl.dsp.exec_cmd("beamer cycle"))
-- SUPER+SHIFT+P: back to mirror instantly (handy in front of a class)
hl.bind("SUPER + SHIFT + P", hl.dsp.exec_cmd("beamer mirror"))
