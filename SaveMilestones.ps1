# Automatic Milestone Saver for GigaLearnCPP
# Saves a permanent copy every N checkpoints (configurable interval)
# Run this script while training to automatically save milestone checkpoints

$checkpointsPath = "build\checkpoints"  # Checkpoints are saved in build/checkpoints
$milestonesPath = "milestones"
$milestoneInterval = 2  # Save milestone every 2 checkpoints
# Note: With current settings (tsPerSave = 22.5M), this saves every ~2.25 billion timesteps

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

Write-Host "Milestone Saver Started - Watching for checkpoints every $milestoneInterval checkpoints" -ForegroundColor Cyan
Write-Host "Checkpoints path: $checkpointsPath" -ForegroundColor Gray
Write-Host "Milestones path: $milestonesPath" -ForegroundColor Gray
Write-Host "Press Ctrl+C to stop" -ForegroundColor Yellow
Write-Host ""

# Monitor checkpoints directory
while ($true) {
    Start-Sleep -Seconds 30  # Check every 30 seconds
    
    if (Test-Path $checkpointsPath) {
        $checkpoints = Get-ChildItem -Path $checkpointsPath -Directory | 
                       Where-Object { $_.Name -match '^\d+$' } |
                       Sort-Object { [long]$_.Name }
        
        if ($checkpoints.Count -gt 0) {
            # Count checkpoints sequentially (1st, 2nd, 3rd, etc.)
            $checkpointIndex = 0
            foreach ($checkpoint in $checkpoints) {
                $checkpointIndex++
                $timesteps = [long]$checkpoint.Name
                
                # Check if this is a milestone (every Nth checkpoint)
                if (($checkpointIndex % $milestoneInterval) -eq 0 -and -not $savedMilestones.ContainsKey($checkpoint.Name)) {
                    $milestoneName = "milestone_checkpoint${checkpointIndex}_${timesteps}"
                    $milestonePath = Join-Path $milestonesPath $milestoneName
                    
                    $timestamp = Get-Date -Format 'HH:mm:ss'
                    Write-Host "[$timestamp] Saving MILESTONE #${checkpointIndex}: $milestoneName ($timesteps timesteps)" -ForegroundColor Green
                    
                    try {
                        Copy-Item -Path $checkpoint.FullName -Destination $milestonePath -Recurse -Force
                        
                        # Mark as saved
                        $savedMilestones[$checkpoint.Name] = $true
                        Add-Content -Path "$milestonesPath\.saved_list.txt" -Value $checkpoint.Name
                        
                        Write-Host "  [OK] Milestone saved successfully to: $milestonePath" -ForegroundColor Green
                    } catch {
                        Write-Host "  [ERROR] Error saving milestone: $_" -ForegroundColor Red
                    }
                }
            }
        }
    } else {
        $timestamp = Get-Date -Format 'HH:mm:ss'
        Write-Host "[$timestamp] Waiting for checkpoints directory: $checkpointsPath" -ForegroundColor Yellow
    }
}
