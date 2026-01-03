# Automatic Milestone Saver for GigaLearnCPP
# Saves a permanent copy every 100 checkpoints (every 1 billion timesteps)

$checkpointsPath = "checkpoints"
$milestonesPath = "milestones"
$milestoneInterval = 100  # Save milestone every 100 checkpoints (100M timesteps with current settings)

# Create milestones directory if it doesn't exist
if (-not (Test-Path $milestonesPath)) {
    New-Item -Path $milestonesPath -ItemType Directory | Out-Null
    Write-Host "Created milestones directory" -ForegroundColor Green
}

# Track which milestones we've already saved
$savedMilestones = @{}
if (Test-Path "$milestonesPath\.saved_list.txt") {
    Get-Content "$milestonesPath\.saved_list.txt" | ForEach-Object {
        $savedMilestones[$_] = $true
    }
}

Write-Host "Milestone Saver Started - Watching for checkpoints every $milestoneInterval iterations" -ForegroundColor Cyan
Write-Host "Press Ctrl+C to stop" -ForegroundColor Yellow
Write-Host ""

# Monitor checkpoints directory
while ($true) {
    Start-Sleep -Seconds 30  # Check every 30 seconds
    
    if (Test-Path $checkpointsPath) {
        $checkpoints = Get-ChildItem -Path $checkpointsPath -Directory | 
                       Where-Object { $_.Name -match '^\d+$' } |
                       Sort-Object { [long]$_.Name }
        
        foreach ($checkpoint in $checkpoints) {
            $timesteps = [long]$checkpoint.Name
            $checkpointNum = $timesteps / 1000000  # Divide by 1M (your tsPerSave)
            
            # Check if this is a milestone (every 100 checkpoints)
            if (($checkpointNum % $milestoneInterval) -eq 0 -and -not $savedMilestones.ContainsKey($checkpoint.Name)) {
                $milestoneName = "milestone_${checkpointNum}_${timesteps}"
                $milestonePath = Join-Path $milestonesPath $milestoneName
                
                Write-Host "[$(Get-Date -Format 'HH:mm:ss')] Saving MILESTONE: $milestoneName ($timesteps timesteps)" -ForegroundColor Green
                
                try {
                    Copy-Item -Path $checkpoint.FullName -Destination $milestonePath -Recurse -Force
                    
                    # Mark as saved
                    $savedMilestones[$checkpoint.Name] = $true
                    Add-Content -Path "$milestonesPath\.saved_list.txt" -Value $checkpoint.Name
                    
                    Write-Host "  ? Milestone saved successfully!" -ForegroundColor Green
                } catch {
                    Write-Host "  ? Error saving milestone: $_" -ForegroundColor Red
                }
            }
        }
    }
}
