# FAST_Pipeline_Hashpipe
This is thread manage pipeline for FAST FRB backend. 
The data are store in a Ram for temperory store. When found a candidate, the data will be store into Disk.
This part only include the former packet receiving, dissambling, and Filterbank data formate convertion.
Required:
  Hashpipe
  Ruby 2.1.2
  Install information: See: https://github.com/peterniuzai/Work_memo.git/hashpipe_install.pdf

Make file  
  This pipeline including c for hashpipe and c++ for filterbank data formate convert(Written by K.J.Li).
  ./make_cmd  
  sudo make install
  
Hashpiepe has a monitor to waitch the system. 
  It is wrriten in Ruby, we could find the hashpipe monitor from 
  https://github.com/david-macmahon/rb-hashpipe.git
  
  After install hashpipe, if you want to run the monitor, you can put hashpipe_status_monitor.rb in terminal.
  

