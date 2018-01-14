<?php
define("DATABASE_TABLE_PLAYERS_ACCOUNTNAME_MAXLENGTH", 32); 
define("DATABASE_TABLE_PLAYERS_PASSWORD_MAXLENGTH", 72);
define("DATABASE_FILEPATH", "/home/core/server/new/userAccounts.db");
define("DATABASE_TABLE_PLAYERS", "Players");
define("DATABASE_TABLE_PLAYERS_PASSWORDSALT_LENGTH", 22);
define("BCRYPT_SALT_PREFIX", "$2y$");
define("BCRYPT_SALT_ROUNDS", "10$");
define("BCRYPT_SALT_PREFIX_AND_ROUNDS_CHARLENGTH", 7);
define("DEFAULT_STARTING_HEALTH", 100);

class UsersDB extends SQLite3
{
    function __construct()
    {
        $this->open(DATABASE_FILEPATH);
    }
}



//Function copied straight away from http://stackoverflow.com/questions/4356289/php-random-string-generator/31107425#31107425
function random_str($length, $keyspace = '0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ./')
{
    $str = '';
    $max = mb_strlen($keyspace, '8bit') - 1;
    for ($i = 0; $i < $length; ++$i) {
        $str .= $keyspace[random_int(0, $max)];
    }
    return $str;
}

$accountName = $_POST["accountName"];
$password = $_POST["password"];
if(strlen($password)>DATABASE_TABLE_PLAYERS_PASSWORD_MAXLENGTH || strlen($accountName)>DATABASE_TABLE_PLAYERS_ACCOUNTNAME_MAXLENGTH || empty($password) || empty($accountName)){
	die();
}
if(!preg_match("/^[a-zA-Z]{1}[a-zA-Z0-9]*$/",$accountName)){
	die(); //Illegal accountName
}
if(preg_match("/\x00/", $password)){
	die(); //Illegal password (contains NULL character)
}
$db = new UsersDB();
$stmtCheckUsernameAvailability = $db->prepare('SELECT * FROM '.DATABASE_TABLE_PLAYERS.' WHERE AccountName=:accountName');
$stmtCheckUsernameAvailability->bindValue(':accountName', $accountName, SQLITE3_TEXT);
$SQLResults = $stmtCheckUsernameAvailability->execute();
if($SQLResults->fetchArray()!=0){
	echo "Username already in use";
	die();
}
$SQLResults->finalize();
$stmtCheckUsernameAvailability->close();
//Add the new username
$salt = random_str(DATABASE_TABLE_PLAYERS_PASSWORDSALT_LENGTH);
$passwordHash = crypt($password, BCRYPT_SALT_PREFIX.BCRYPT_SALT_ROUNDS.$salt);
$stmtAddNewPlayer = $db->prepare('INSERT INTO '.DATABASE_TABLE_PLAYERS.' (accountName, password, salt, health) VALUES (:accountName, :passwordHash, :salt, '.DEFAULT_STARTING_HEALTH.')');
$stmtAddNewPlayer->bindValue(':accountName', $accountName, SQLITE3_TEXT);
$stmtAddNewPlayer->bindValue(':passwordHash', substr($passwordHash, BCRYPT_SALT_PREFIX_AND_ROUNDS_CHARLENGTH + DATABASE_TABLE_PLAYERS_PASSWORDSALT_LENGTH - 1), SQLITE3_TEXT);
$stmtAddNewPlayer->bindValue(':salt', $salt, SQLITE3_TEXT);
$stmtAddNewPlayer->execute();
$stmtAddNewPlayer->close();
$db->close();
echo "New user registered successfully";
?>

