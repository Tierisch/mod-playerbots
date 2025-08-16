DROP TABLE IF EXISTS `playerbots_dungeon_waypoint`;
CREATE TABLE `playerbots_dungeon_waypoint` (
    `id` INT AUTO_INCREMENT PRIMARY KEY,
    `map_id` INT NOT NULL,
    `dungeon_name` VARCHAR(64) NOT NULL,
    `order_index` INT NOT NULL,
    `x` FLOAT NOT NULL,
    `y` FLOAT NOT NULL,
    `z` FLOAT NOT NULL,
    `jump` TINYINT(1) DEFAULT 0,
    `pause` INT DEFAULT 0,
    `healer_mana_pct` FLOAT DEFAULT 0.6,
    `next_index` INT DEFAULT 0,
    `interact_type` TINYINT(1) DEFAULT 0,
    `interact_guid` INT DEFAULT 0,
    `interact_param` INT DEFAULT 0,
    `comment` VARCHAR(255) DEFAULT '',
    `tell` TINYINT(1) DEFAULT 0
);
