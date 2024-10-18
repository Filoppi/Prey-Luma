# Prompt the user for a name
$prey_bin_path = Read-Host "Please enter the directory of your Prey's installation binaries folder`n(e.g. `"C:\Program Files (x86)\Steam\steamapps\common\Prey\Binaries\Danielle\x64\Release`")"

# Try to set the environment variable and handle errors if it fails
try {
	# Set the path in a (user) environment variable
	[System.Environment]::SetEnvironmentVariable("PREY_BIN_PATH", $prey_bin_path, [System.EnvironmentVariableTarget]::User)
    
	# Display a confirmation message
	Write-Host "The `"PREY_BIN_PATH`" user environment variable has been succesfully saved."
} 
catch {
	# If something goes wrong, display the error
	Write-Host "Failed to set the environment variable. Error details: $($_.Exception.Message)" -ForegroundColor Red
}

# Wait so the user can see the messages above
Write-Host -NoNewLine 'Press any key to continue...';
$null = $Host.UI.RawUI.ReadKey('NoEcho,IncludeKeyDown');