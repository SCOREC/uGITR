./test_smallE_largeP.sh &> smallE_largeP.txt
python output_convert.py smallE_largeP.txt smallE_largeP_rebuild.dat smallE_largeP_push.dat
echo "test_smallE_largeP DONE"

./test_largeE_smallP.sh &> largeE_smallP.txt
python output_convert.py largeE_smallP.txt largeE_smallP_rebuild.dat largeE_smallP_push.dat
echo "test_largeE_smallP DONE"

./test_largeE_largeP.sh &> largeE_largeP.txt
python output_convert.py largeE_largeP.txt largeE_largeP_rebuild.dat largeE_largeP_push.dat
echo "test_largeE_largeP DONE"