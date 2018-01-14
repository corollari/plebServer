<?php
define("DATABASE_TABLE_PLAYERS_ACCOUNTNAME_MAXLENGTH", 32); 
define("DATABASE_TABLE_PLAYERS_PASSWORD_MAXLENGTH", 72);
define("DATABASE_FILEPATH", "/home/core/server/new/userAccounts.db");
define("DATABASE_TABLE_PLAYERS", "Players");

class UsersDB extends SQLite3
{
    function __construct()
    {
        $this->open(DATABASE_FILEPATH);
    }
}


$accountName = $_POST["accountName"];

$db = new UsersDB();
$stmtCheckUsernameAvailability = $db->prepare('SELECT * FROM '.DATABASE_TABLE_PLAYERS.' WHERE AccountName=:accountName');
$stmtCheckUsernameAvailability->bindValue(':accountName', $accountName, SQLITE3_TEXT);
$SQLResults = $stmtCheckUsernameAvailability->execute();
if($SQLResults->fetchArray()!=0){
	echo "";
}
else{
	echo "available";
}
$SQLResults->finalize();
$stmtCheckUsernameAvailability->close();
$db->close();
die();
?>

