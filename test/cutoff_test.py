#!/usr/bin/env python2
"""With modified firmware, run a battery cutoff stress test."""
import datetime
import subprocess
import time

from SimpleCV import *

THRESHOLD = 220 # Threshold value used for values of "blue"..
MASK_THRESHOLD = 100 # Threshold for how much of the image is blue.

def main():
  iteration = 1
  # Open the USB webcam.
  c = Camera(0)

  dut_list = {'100.107.2.49' : 'left DUT',
              '100.107.2.245' : 'right DUT',
              }

  while True:
    print "Iteration: %d (%s)" % (iteration, datetime.datetime.today())

    # Enable USB power.  This will turn on the DUT's AC adapter.
    # subprocess.call("cros_sdk dut-control prtctl4_pwren:on", shell=True)
    # Turn on the DUTs.
    subprocess.call('cros_sdk dut-control -p 9999 power_state:on', shell=True)
    subprocess.call('cros_sdk dut-control -p 9998 power_state:on', shell=True)

    # Wait for DUT to boot to login screen.
    time.sleep(25)

    # Check dmesg for errors.
    # duts_with_issues = check_dmesg_for_errors(['100.107.2.49', '100.107.2.245'],
    #                                           iteration)
    # if duts_with_issues:
    #   for dut in duts_with_issues:
    #     print '%s had the error string in dmesg!' % dut_list[dut]

    # Check that login screen is actually displayed.
    if not login_screen_is_displayed(c):
      # Keep screen "awake" by pressing the left control key.
      print "Login screen not detected.  Keeping DUT up."
      while True:
        subprocess.call('cros_sdk dut-control -p 9999 '
                        'ec_uart_cmd:"kbpress 0 2"', shell=True)
        subprocess.call('cros_sdk dut-control -p 9998 '
                        'ec_uart_cmd:"kbpress 0 2"', shell=True)
        time.sleep(2)

    # Cutoff battery and wait a bit for system to lose power completely.
    # subprocess.call("cros_sdk dut-control prtctl4_pwren:off".split())
    # time.sleep(10)

    # Shut the DUTs down.
    subprocess.call('cros_sdk dut-control -p 9999 '
                    'power_state:off', shell=True)
    subprocess.call('cros_sdk dut-control -p 9998 '
                    'power_state:off', shell=True)
    iteration += 1

def login_screen_is_displayed(camera):
  """Checks for enough "blue" present in the camera's FOV.

     The login screen is pretty blue, so I have the camera set up to watch the
     lower corners of the display.

     Args:
       camera: A Camera object.

     Returns whether or not there is enough blue in the photo.
  """
  i = camera.getImage()
  r, g, b = i.splitChannels()
  # We only really care about the blue channel.
  mask = b.threshold(THRESHOLD)
  print mask.meanColor()[0]

  likely_login_screen = mask.meanColor()[0] > MASK_THRESHOLD
  if not likely_login_screen:
    # Save a photo of the failure.
    cwd = os.getcwd() + '/'
    i.save(cwd + 'test_fail.jpg')

  return likely_login_screen

def check_dmesg_for_errors(ip_list, iteration):
  """Login to DUT and check for a string present in dmesg.

    Args:
      ip_list: A list of strings containing the IP addresses of the DUTs.
      iteration: An integer representing the current iteration.
  """
  duts_with_issues = []

  for ip in ip_list:
    # ssh into DUT.
    err_string = 'failed to reset reg 0x'
    ssh_cmd = 'ssh -i ~/.ssh/testing_rsa root@' + ip + ' '
    cmd = ssh_cmd + '"dmesg | grep \\"'+ err_string + '\\""'

    # run dmesg command and grep for error.
    try:
      if subprocess.check_output(cmd, shell=True):
        # Copy dmesg from the DUT.
        subprocess.call(ssh_cmd + '"dmesg > /tmp/dmesg.log"', shell=True)
        subprocess.call('scp -i ~/.ssh/testing_rsa root@' + ip +
                        ':/tmp/dmesg.log ./dmesg_%s_it%d.log' %
                        (ip.replace('.', '_'), iteration), shell=True)
        duts_with_issues.append(ip)
        break

    except subprocess.CalledProcessError:
      pass # ignore if it's not found.

  # Return a list of the IPs that had issues.
  return duts_with_issues

if __name__ == '__main__':
  main()
