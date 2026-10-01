CREATE TABLE IF NOT EXISTS wardrobe_accounts (
 account_id INT NOT NULL PRIMARY KEY
) ENGINE=InnoDB;
CREATE TABLE IF NOT EXISTS wardrobe_skins (
 account_id INT NOT NULL, item_id INT NOT NULL, unlocked_at BIGINT NOT NULL,
 source_character INT NOT NULL, PRIMARY KEY (account_id,item_id)
) ENGINE=InnoDB;
CREATE TABLE IF NOT EXISTS wardrobe_outfits (
 account_id INT NOT NULL, name VARCHAR(32) NOT NULL, skins_json TEXT NOT NULL,
 updated_at BIGINT NOT NULL, PRIMARY KEY (account_id,name)
) ENGINE=InnoDB;
CREATE TABLE IF NOT EXISTS wardrobe_requests (
 request_id VARCHAR(64) NOT NULL PRIMARY KEY, account_id INT NOT NULL,
 result VARCHAR(512) NOT NULL, created_at BIGINT NOT NULL,
 INDEX receipt_expiry (created_at)
) ENGINE=InnoDB;
