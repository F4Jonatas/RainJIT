<#
.SYNOPSIS
	Lightweight .env file loader and writer for PowerShell.

.DESCRIPTION
	DotEnv is a static utility class used to read, manipulate, and persist
	environment variables stored in a .env file.

	All variables are applied ONLY to the current PowerShell session
	via the env: provider. No system-wide or user-wide persistence
	is performed.

	The class maintains an internal in-memory cache that represents
	the current state of the .env file.

.DESIGN
	- Static-only API (no instantiation via new())
	- Single internal state (singleton-style)
	- Deterministic behavior
	- Explicit I/O (Read / Save)
	- No side effects outside the current session

.EXAMPLE
	[dotenv]::read(".env")
	[dotenv]::get("APP_ENV")
	[dotenv]::set("APP_ENV", "development")
	[dotenv]::save(".env")
#>

class dotenv {

	# Path of the currently loaded .env file
	static hidden [string] $FilePath = ""

	# In-memory key/value representation of the .env file
	static hidden [hashtable] $Data = @{}

	<#
	.SYNOPSIS
		Reads a .env file and applies variables to the current session.

	.DESCRIPTION
		Parses the specified .env file line by line and loads all valid
		key/value pairs into memory.

		Each variable is immediately applied to the current PowerShell
		session using the env: provider.

		- Empty lines are ignored
		- Lines starting with '#' are treated as comments
		- Only the first '=' is used as the key/value separator
		- Surrounding quotes in values are removed

	.PARAMETER FilePath
		Path to the .env file to be loaded.

	.NOTES
		Calling read() resets the internal state before loading.
	#>
	static [void] read([string] $FilePath) {
		[dotenv]::FilePath = $FilePath
		[dotenv]::Data.Clear()

		if (-not (Test-Path $FilePath)) {
			return
		}

		foreach ($line in Get-Content $FilePath) {

			$line = $line.Trim()

			# Ignore empty lines and comments
			if ([string]::IsNullOrWhiteSpace($line) -or $line.StartsWith('#')) {
				continue
			}

			# Split only on the first '='
			$key, $value = $line -split '=', 2

			if ([string]::IsNullOrWhiteSpace($key)) {
				continue
			}

			$key   = $key.Trim()
			$value = $value.Trim()

			# Remove surrounding single or double quotes
			if (
				($value.StartsWith('"') -and $value.EndsWith('"')) -or
				($value.StartsWith("'") -and $value.EndsWith("'"))
			) {
				$value = $value.Substring(1, $value.Length - 2)
			}

			# Store in memory
			[dotenv]::Data[$key] = $value

			# Apply only to the current PowerShell session
			Set-Item -Path "env:$key" -Value $value
		}
	}

	<#
	.SYNOPSIS
		Retrieves a value from the internal cache.

	.DESCRIPTION
		Returns the value associated with the given key as loaded
		from the .env file or set during runtime.

		This method does NOT read from env: directly.
		It reads from the in-memory representation.

	.PARAMETER Key
		Name of the environment variable.

	.OUTPUTS
		System.String
	#>
	static [string] get([string] $Key) {
		return [dotenv]::Data[$Key]
	}

	<#
	.SYNOPSIS
		Sets or updates a variable in memory and the current session.

	.DESCRIPTION
		Updates the internal cache and immediately applies the value
		to the current PowerShell session.

		The value is NOT persisted to disk until save() is called.

	.PARAMETER Key
		Name of the environment variable.

	.PARAMETER Value
		Value to assign to the variable.
	#>
	static [void] set([string] $Key, [string] $Value) {

		[dotenv]::Data[$key] = $Value
		Set-Item -Path "env:$Key" -Value $Value
	}

	<#
	.SYNOPSIS
		Persists the current state to a .env file.

	.DESCRIPTION
		Writes the internal key/value cache to disk in the format:

			KEY=value

		If no file path is provided, the previously loaded file path
		is used.

	.PARAMETER FilePath
		Optional path to save the .env file.
		Overrides the previously loaded path if provided.

	.NOTES
		Comments and original ordering are not preserved.
	#>
	static [void] save([string] $FilePath = $null) {

		$path = if ($FilePath) {
			$FilePath
		} else {
			[dotenv]::FilePath
		}

		if (-not $path) {
			throw "dotenv.save(): file path not defined."
		}

		$lines = foreach ($key in [dotenv]::Data.Keys) {
			"$key=$([dotenv]::Data[$key])"
		}

		Set-Content -Path $path -Value $lines -Encoding UTF8
	}
}
