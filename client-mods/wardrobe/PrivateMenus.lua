function PrivateWardrobe_OnLoad()
    PrivateWardrobeBrowser:CreateWebView();
    PrivateWardrobe:Hide();
end

function PrivateWardrobe_Open()
    PrivateWardrobe:Show();
    PrivateWardrobe:SetRect(0, 0, 1280, 960);
    PrivateWardrobeBrowser:LoadUrlWithWebAuth(PRIVATE_WARDROBE_URL);
end

function PrivateMenus_Register()
    SlashCmdList["PRIVATEWARDROBE"] = PrivateWardrobe_Open;
    SLASH_PRIVATEWARDROBE1 = "/wardrobe";
    RegisterMenu(PRIVATE_WARDROBE_LABEL, SLASH_PRIVATEWARDROBE1, PRIVATE_WARDROBE_ICON);
end
