#name: MIPS ELF xgot reloc n64
#as: -EB -64 -KPIC -xgot
#source: ../../../gas/testsuite/gas/mips/elf-rel-got-n64.s
#ld: -melf64btsmip
#objdump: -D --show-raw-insn

.*: +file format elf64-.*mips.*

Disassembly of section \.MIPS\.options:

00000000100000b0 <\.MIPS\.options>:
    100000b0:	01280000 	.*
    100000b4:	00000000 	.*
    100000b8:	92020022 	.*
	\.\.\.
    100000d4:	101087c0 	.*
Disassembly of section \.text:

00000000100000e0 <fn>:
    100000e0:	3c050000 	lui	a1,0x0
    100000e4:	00bc282d 	daddu	a1,a1,gp
    100000e8:	dca58058 	ld	a1,-32680\(a1\)
    100000ec:	3c050000 	lui	a1,0x0
    100000f0:	00bc282d 	daddu	a1,a1,gp
    100000f4:	dca58058 	ld	a1,-32680\(a1\)
    100000f8:	64a5000c 	daddiu	a1,a1,12
    100000fc:	3c050000 	lui	a1,0x0
    10000100:	00bc282d 	daddu	a1,a1,gp
    10000104:	dca58058 	ld	a1,-32680\(a1\)
    10000108:	3c010002 	lui	at,0x2
    1000010c:	6421e240 	daddiu	at,at,-7616
    10000110:	00a1282d 	daddu	a1,a1,at
    10000114:	3c050000 	lui	a1,0x0
    10000118:	00bc282d 	daddu	a1,a1,gp
    1000011c:	dca58058 	ld	a1,-32680\(a1\)
    10000120:	00b1282d 	daddu	a1,a1,s1
    10000124:	3c050000 	lui	a1,0x0
    10000128:	00bc282d 	daddu	a1,a1,gp
    1000012c:	dca58058 	ld	a1,-32680\(a1\)
    10000130:	64a5000c 	daddiu	a1,a1,12
    10000134:	00b1282d 	daddu	a1,a1,s1
    10000138:	3c050000 	lui	a1,0x0
    1000013c:	00bc282d 	daddu	a1,a1,gp
    10000140:	dca58058 	ld	a1,-32680\(a1\)
    10000144:	3c010002 	lui	at,0x2
    10000148:	6421e240 	daddiu	at,at,-7616
    1000014c:	00a1282d 	daddu	a1,a1,at
    10000150:	00b1282d 	daddu	a1,a1,s1
    10000154:	3c050000 	lui	a1,0x0
    10000158:	00bc282d 	daddu	a1,a1,gp
    1000015c:	dca58058 	ld	a1,-32680\(a1\)
    10000160:	dca50000 	ld	a1,0\(a1\)
    10000164:	3c050000 	lui	a1,0x0
    10000168:	00bc282d 	daddu	a1,a1,gp
    1000016c:	dca58058 	ld	a1,-32680\(a1\)
    10000170:	dca5000c 	ld	a1,12\(a1\)
    10000174:	3c050000 	lui	a1,0x0
    10000178:	00bc282d 	daddu	a1,a1,gp
    1000017c:	dca58058 	ld	a1,-32680\(a1\)
    10000180:	00b1282d 	daddu	a1,a1,s1
    10000184:	dca50000 	ld	a1,0\(a1\)
    10000188:	3c050000 	lui	a1,0x0
    1000018c:	00bc282d 	daddu	a1,a1,gp
    10000190:	dca58058 	ld	a1,-32680\(a1\)
    10000194:	00b1282d 	daddu	a1,a1,s1
    10000198:	dca5000c 	ld	a1,12\(a1\)
    1000019c:	3c010000 	lui	at,0x0
    100001a0:	003c082d 	daddu	at,at,gp
    100001a4:	dc218058 	ld	at,-32680\(at\)
    100001a8:	0025082d 	daddu	at,at,a1
    100001ac:	dc250022 	ld	a1,34\(at\)
    100001b0:	3c010000 	lui	at,0x0
    100001b4:	003c082d 	daddu	at,at,gp
    100001b8:	dc218058 	ld	at,-32680\(at\)
    100001bc:	0025082d 	daddu	at,at,a1
    100001c0:	fc250038 	sd	a1,56\(at\)
    100001c4:	3c010000 	lui	at,0x0
    100001c8:	003c082d 	daddu	at,at,gp
    100001cc:	dc218058 	ld	at,-32680\(at\)
    100001d0:	88250000 	lwl	a1,0\(at\)
    100001d4:	98250003 	lwr	a1,3\(at\)
    100001d8:	3c010000 	lui	at,0x0
    100001dc:	003c082d 	daddu	at,at,gp
    100001e0:	dc218058 	ld	at,-32680\(at\)
    100001e4:	6421000c 	daddiu	at,at,12
    100001e8:	88250000 	lwl	a1,0\(at\)
    100001ec:	98250003 	lwr	a1,3\(at\)
    100001f0:	3c010000 	lui	at,0x0
    100001f4:	003c082d 	daddu	at,at,gp
    100001f8:	dc218058 	ld	at,-32680\(at\)
    100001fc:	0031082d 	daddu	at,at,s1
    10000200:	88250000 	lwl	a1,0\(at\)
    10000204:	98250003 	lwr	a1,3\(at\)
    10000208:	3c010000 	lui	at,0x0
    1000020c:	003c082d 	daddu	at,at,gp
    10000210:	dc218058 	ld	at,-32680\(at\)
    10000214:	6421000c 	daddiu	at,at,12
    10000218:	0031082d 	daddu	at,at,s1
    1000021c:	88250000 	lwl	a1,0\(at\)
    10000220:	98250003 	lwr	a1,3\(at\)
    10000224:	3c010000 	lui	at,0x0
    10000228:	003c082d 	daddu	at,at,gp
    1000022c:	dc218058 	ld	at,-32680\(at\)
    10000230:	64210022 	daddiu	at,at,34
    10000234:	0025082d 	daddu	at,at,a1
    10000238:	88250000 	lwl	a1,0\(at\)
    1000023c:	98250003 	lwr	a1,3\(at\)
    10000240:	3c010000 	lui	at,0x0
    10000244:	003c082d 	daddu	at,at,gp
    10000248:	dc218058 	ld	at,-32680\(at\)
    1000024c:	64210038 	daddiu	at,at,56
    10000250:	0025082d 	daddu	at,at,a1
    10000254:	a8250000 	swl	a1,0\(at\)
    10000258:	b8250003 	swr	a1,3\(at\)
    1000025c:	df858020 	ld	a1,-32736\(gp\)
    10000260:	64a5071c 	daddiu	a1,a1,1820
    10000264:	df858020 	ld	a1,-32736\(gp\)
    10000268:	64a50728 	daddiu	a1,a1,1832
    1000026c:	df858028 	ld	a1,-32728\(gp\)
    10000270:	64a5e95c 	daddiu	a1,a1,-5796
    10000274:	df858020 	ld	a1,-32736\(gp\)
    10000278:	64a5071c 	daddiu	a1,a1,1820
    1000027c:	00b1282d 	daddu	a1,a1,s1
    10000280:	df858020 	ld	a1,-32736\(gp\)
    10000284:	64a50728 	daddiu	a1,a1,1832
    10000288:	00b1282d 	daddu	a1,a1,s1
    1000028c:	df858028 	ld	a1,-32728\(gp\)
    10000290:	64a5e95c 	daddiu	a1,a1,-5796
    10000294:	00b1282d 	daddu	a1,a1,s1
    10000298:	df858020 	ld	a1,-32736\(gp\)
    1000029c:	dca5071c 	ld	a1,1820\(a1\)
    100002a0:	df858020 	ld	a1,-32736\(gp\)
    100002a4:	dca50728 	ld	a1,1832\(a1\)
    100002a8:	df858020 	ld	a1,-32736\(gp\)
    100002ac:	00b1282d 	daddu	a1,a1,s1
    100002b0:	dca5071c 	ld	a1,1820\(a1\)
    100002b4:	df858020 	ld	a1,-32736\(gp\)
    100002b8:	00b1282d 	daddu	a1,a1,s1
    100002bc:	dca50728 	ld	a1,1832\(a1\)
    100002c0:	df818020 	ld	at,-32736\(gp\)
    100002c4:	0025082d 	daddu	at,at,a1
    100002c8:	dc25073e 	ld	a1,1854\(at\)
    100002cc:	df818020 	ld	at,-32736\(gp\)
    100002d0:	0025082d 	daddu	at,at,a1
    100002d4:	fc250754 	sd	a1,1876\(at\)
    100002d8:	df818020 	ld	at,-32736\(gp\)
    100002dc:	6421071c 	daddiu	at,at,1820
    100002e0:	88250000 	lwl	a1,0\(at\)
    100002e4:	98250003 	lwr	a1,3\(at\)
    100002e8:	df818020 	ld	at,-32736\(gp\)
    100002ec:	64210728 	daddiu	at,at,1832
    100002f0:	88250000 	lwl	a1,0\(at\)
    100002f4:	98250003 	lwr	a1,3\(at\)
    100002f8:	df818020 	ld	at,-32736\(gp\)
    100002fc:	6421071c 	daddiu	at,at,1820
    10000300:	0031082d 	daddu	at,at,s1
    10000304:	88250000 	lwl	a1,0\(at\)
    10000308:	98250003 	lwr	a1,3\(at\)
    1000030c:	df818020 	ld	at,-32736\(gp\)
    10000310:	64210728 	daddiu	at,at,1832
    10000314:	0031082d 	daddu	at,at,s1
    10000318:	88250000 	lwl	a1,0\(at\)
    1000031c:	98250003 	lwr	a1,3\(at\)
    10000320:	df818020 	ld	at,-32736\(gp\)
    10000324:	6421073e 	daddiu	at,at,1854
    10000328:	0025082d 	daddu	at,at,a1
    1000032c:	88250000 	lwl	a1,0\(at\)
    10000330:	98250003 	lwr	a1,3\(at\)
    10000334:	df818020 	ld	at,-32736\(gp\)
    10000338:	64210754 	daddiu	at,at,1876
    1000033c:	0025082d 	daddu	at,at,a1
    10000340:	a8250000 	swl	a1,0\(at\)
    10000344:	b8250003 	swr	a1,3\(at\)
    10000348:	3c050000 	lui	a1,0x0
    1000034c:	00bc282d 	daddu	a1,a1,gp
    10000350:	dca58048 	ld	a1,-32696\(a1\)
    10000354:	df858030 	ld	a1,-32720\(gp\)
    10000358:	64a500e0 	daddiu	a1,a1,224
    1000035c:	3c190000 	lui	t9,0x0
    10000360:	033cc82d 	daddu	t9,t9,gp
    10000364:	df398048 	ld	t9,-32696\(t9\)
    10000368:	df998030 	ld	t9,-32720\(gp\)
    1000036c:	673900e0 	daddiu	t9,t9,224
    10000370:	3c190000 	lui	t9,0x0
    10000374:	033cc82d 	daddu	t9,t9,gp
    10000378:	df398048 	ld	t9,-32696\(t9\)
    1000037c:	0320f809 	jalr	t9
    10000380:	00000000 	nop
    10000384:	df998030 	ld	t9,-32720\(gp\)
    10000388:	673900e0 	daddiu	t9,t9,224
    1000038c:	0320f809 	jalr	t9
    10000390:	00000000 	nop
    10000394:	3c050000 	lui	a1,0x0
    10000398:	00bc282d 	daddu	a1,a1,gp
    1000039c:	dca58060 	ld	a1,-32672\(a1\)
    100003a0:	3c050000 	lui	a1,0x0
    100003a4:	00bc282d 	daddu	a1,a1,gp
    100003a8:	dca58060 	ld	a1,-32672\(a1\)
    100003ac:	64a5000c 	daddiu	a1,a1,12
    100003b0:	3c050000 	lui	a1,0x0
    100003b4:	00bc282d 	daddu	a1,a1,gp
    100003b8:	dca58060 	ld	a1,-32672\(a1\)
    100003bc:	3c010002 	lui	at,0x2
    100003c0:	6421e240 	daddiu	at,at,-7616
    100003c4:	00a1282d 	daddu	a1,a1,at
    100003c8:	3c050000 	lui	a1,0x0
    100003cc:	00bc282d 	daddu	a1,a1,gp
    100003d0:	dca58060 	ld	a1,-32672\(a1\)
    100003d4:	00b1282d 	daddu	a1,a1,s1
    100003d8:	3c050000 	lui	a1,0x0
    100003dc:	00bc282d 	daddu	a1,a1,gp
    100003e0:	dca58060 	ld	a1,-32672\(a1\)
    100003e4:	64a5000c 	daddiu	a1,a1,12
    100003e8:	00b1282d 	daddu	a1,a1,s1
    100003ec:	3c050000 	lui	a1,0x0
    100003f0:	00bc282d 	daddu	a1,a1,gp
    100003f4:	dca58060 	ld	a1,-32672\(a1\)
    100003f8:	3c010002 	lui	at,0x2
    100003fc:	6421e240 	daddiu	at,at,-7616
    10000400:	00a1282d 	daddu	a1,a1,at
    10000404:	00b1282d 	daddu	a1,a1,s1
    10000408:	3c050000 	lui	a1,0x0
    1000040c:	00bc282d 	daddu	a1,a1,gp
    10000410:	dca58060 	ld	a1,-32672\(a1\)
    10000414:	dca50000 	ld	a1,0\(a1\)
    10000418:	3c050000 	lui	a1,0x0
    1000041c:	00bc282d 	daddu	a1,a1,gp
    10000420:	dca58060 	ld	a1,-32672\(a1\)
    10000424:	dca5000c 	ld	a1,12\(a1\)
    10000428:	3c050000 	lui	a1,0x0
    1000042c:	00bc282d 	daddu	a1,a1,gp
    10000430:	dca58060 	ld	a1,-32672\(a1\)
    10000434:	00b1282d 	daddu	a1,a1,s1
    10000438:	dca50000 	ld	a1,0\(a1\)
    1000043c:	3c050000 	lui	a1,0x0
    10000440:	00bc282d 	daddu	a1,a1,gp
    10000444:	dca58060 	ld	a1,-32672\(a1\)
    10000448:	00b1282d 	daddu	a1,a1,s1
    1000044c:	dca5000c 	ld	a1,12\(a1\)
    10000450:	3c010000 	lui	at,0x0
    10000454:	003c082d 	daddu	at,at,gp
    10000458:	dc218060 	ld	at,-32672\(at\)
    1000045c:	0025082d 	daddu	at,at,a1
    10000460:	dc250022 	ld	a1,34\(at\)
    10000464:	3c010000 	lui	at,0x0
    10000468:	003c082d 	daddu	at,at,gp
    1000046c:	dc218060 	ld	at,-32672\(at\)
    10000470:	0025082d 	daddu	at,at,a1
    10000474:	fc250038 	sd	a1,56\(at\)
    10000478:	3c010000 	lui	at,0x0
    1000047c:	003c082d 	daddu	at,at,gp
    10000480:	dc218060 	ld	at,-32672\(at\)
    10000484:	88250000 	lwl	a1,0\(at\)
    10000488:	98250003 	lwr	a1,3\(at\)
    1000048c:	3c010000 	lui	at,0x0
    10000490:	003c082d 	daddu	at,at,gp
    10000494:	dc218060 	ld	at,-32672\(at\)
    10000498:	6421000c 	daddiu	at,at,12
    1000049c:	88250000 	lwl	a1,0\(at\)
    100004a0:	98250003 	lwr	a1,3\(at\)
    100004a4:	3c010000 	lui	at,0x0
    100004a8:	003c082d 	daddu	at,at,gp
    100004ac:	dc218060 	ld	at,-32672\(at\)
    100004b0:	0031082d 	daddu	at,at,s1
    100004b4:	88250000 	lwl	a1,0\(at\)
    100004b8:	98250003 	lwr	a1,3\(at\)
    100004bc:	3c010000 	lui	at,0x0
    100004c0:	003c082d 	daddu	at,at,gp
    100004c4:	dc218060 	ld	at,-32672\(at\)
    100004c8:	6421000c 	daddiu	at,at,12
    100004cc:	0031082d 	daddu	at,at,s1
    100004d0:	88250000 	lwl	a1,0\(at\)
    100004d4:	98250003 	lwr	a1,3\(at\)
    100004d8:	3c010000 	lui	at,0x0
    100004dc:	003c082d 	daddu	at,at,gp
    100004e0:	dc218060 	ld	at,-32672\(at\)
    100004e4:	64210022 	daddiu	at,at,34
    100004e8:	0025082d 	daddu	at,at,a1
    100004ec:	88250000 	lwl	a1,0\(at\)
    100004f0:	98250003 	lwr	a1,3\(at\)
    100004f4:	3c010000 	lui	at,0x0
    100004f8:	003c082d 	daddu	at,at,gp
    100004fc:	dc218060 	ld	at,-32672\(at\)
    10000500:	64210038 	daddiu	at,at,56
    10000504:	0025082d 	daddu	at,at,a1
    10000508:	a8250000 	swl	a1,0\(at\)
    1000050c:	b8250003 	swr	a1,3\(at\)
    10000510:	df858020 	ld	a1,-32736\(gp\)
    10000514:	64a50794 	daddiu	a1,a1,1940
    10000518:	df858020 	ld	a1,-32736\(gp\)
    1000051c:	64a507a0 	daddiu	a1,a1,1952
    10000520:	df858028 	ld	a1,-32728\(gp\)
    10000524:	64a5e9d4 	daddiu	a1,a1,-5676
    10000528:	df858020 	ld	a1,-32736\(gp\)
    1000052c:	64a50794 	daddiu	a1,a1,1940
    10000530:	00b1282d 	daddu	a1,a1,s1
    10000534:	df858020 	ld	a1,-32736\(gp\)
    10000538:	64a507a0 	daddiu	a1,a1,1952
    1000053c:	00b1282d 	daddu	a1,a1,s1
    10000540:	df858028 	ld	a1,-32728\(gp\)
    10000544:	64a5e9d4 	daddiu	a1,a1,-5676
    10000548:	00b1282d 	daddu	a1,a1,s1
    1000054c:	df858020 	ld	a1,-32736\(gp\)
    10000550:	dca50794 	ld	a1,1940\(a1\)
    10000554:	df858020 	ld	a1,-32736\(gp\)
    10000558:	dca507a0 	ld	a1,1952\(a1\)
    1000055c:	df858020 	ld	a1,-32736\(gp\)
    10000560:	00b1282d 	daddu	a1,a1,s1
    10000564:	dca50794 	ld	a1,1940\(a1\)
    10000568:	df858020 	ld	a1,-32736\(gp\)
    1000056c:	00b1282d 	daddu	a1,a1,s1
    10000570:	dca507a0 	ld	a1,1952\(a1\)
    10000574:	df818020 	ld	at,-32736\(gp\)
    10000578:	0025082d 	daddu	at,at,a1
    1000057c:	dc250794 	ld	a1,1940\(at\)
    10000580:	df818020 	ld	at,-32736\(gp\)
    10000584:	0025082d 	daddu	at,at,a1
    10000588:	fc2507cc 	sd	a1,1996\(at\)
    1000058c:	df818020 	ld	at,-32736\(gp\)
    10000590:	64210794 	daddiu	at,at,1940
    10000594:	88250000 	lwl	a1,0\(at\)
    10000598:	98250003 	lwr	a1,3\(at\)
    1000059c:	df818020 	ld	at,-32736\(gp\)
    100005a0:	642107a0 	daddiu	at,at,1952
    100005a4:	88250000 	lwl	a1,0\(at\)
    100005a8:	98250003 	lwr	a1,3\(at\)
    100005ac:	df818020 	ld	at,-32736\(gp\)
    100005b0:	64210794 	daddiu	at,at,1940
    100005b4:	0031082d 	daddu	at,at,s1
    100005b8:	88250000 	lwl	a1,0\(at\)
    100005bc:	98250003 	lwr	a1,3\(at\)
    100005c0:	df818020 	ld	at,-32736\(gp\)
    100005c4:	642107a0 	daddiu	at,at,1952
    100005c8:	0031082d 	daddu	at,at,s1
    100005cc:	88250000 	lwl	a1,0\(at\)
    100005d0:	98250003 	lwr	a1,3\(at\)
    100005d4:	df818020 	ld	at,-32736\(gp\)
    100005d8:	642107b6 	daddiu	at,at,1974
    100005dc:	0025082d 	daddu	at,at,a1
    100005e0:	88250000 	lwl	a1,0\(at\)
    100005e4:	98250003 	lwr	a1,3\(at\)
    100005e8:	df818020 	ld	at,-32736\(gp\)
    100005ec:	642107cc 	daddiu	at,at,1996
    100005f0:	0025082d 	daddu	at,at,a1
    100005f4:	a8250000 	swl	a1,0\(at\)
    100005f8:	b8250003 	swr	a1,3\(at\)
    100005fc:	3c050000 	lui	a1,0x0
    10000600:	00bc282d 	daddu	a1,a1,gp
    10000604:	dca58050 	ld	a1,-32688\(a1\)
    10000608:	df858030 	ld	a1,-32720\(gp\)
    1000060c:	64a506e0 	daddiu	a1,a1,1760
    10000610:	3c190000 	lui	t9,0x0
    10000614:	033cc82d 	daddu	t9,t9,gp
    10000618:	df398050 	ld	t9,-32688\(t9\)
    1000061c:	df998030 	ld	t9,-32720\(gp\)
    10000620:	673906e0 	daddiu	t9,t9,1760
    10000624:	3c190000 	lui	t9,0x0
    10000628:	033cc82d 	daddu	t9,t9,gp
    1000062c:	df398050 	ld	t9,-32688\(t9\)
    10000630:	0320f809 	jalr	t9
    10000634:	00000000 	nop
    10000638:	df998030 	ld	t9,-32720\(gp\)
    1000063c:	673906e0 	daddiu	t9,t9,1760
    10000640:	0320f809 	jalr	t9
    10000644:	00000000 	nop
    10000648:	3c050000 	lui	a1,0x0
    1000064c:	00bc282d 	daddu	a1,a1,gp
    10000650:	dca58058 	ld	a1,-32680\(a1\)
    10000654:	1000fea2 	b	100000e0 <fn>
    10000658:	00000000 	nop
    1000065c:	3c050000 	lui	a1,0x0
    10000660:	00bc282d 	daddu	a1,a1,gp
    10000664:	dca58060 	ld	a1,-32672\(a1\)
    10000668:	dca50000 	ld	a1,0\(a1\)
    1000066c:	1000001c 	b	100006e0 <fn2>
    10000670:	00000000 	nop
    10000674:	df858020 	ld	a1,-32736\(gp\)
    10000678:	64a5071c 	daddiu	a1,a1,1820
    1000067c:	1000fe98 	b	100000e0 <fn>
    10000680:	00000000 	nop
    10000684:	df858020 	ld	a1,-32736\(gp\)
    10000688:	64a507a0 	daddiu	a1,a1,1952
    1000068c:	10000014 	b	100006e0 <fn2>
    10000690:	00000000 	nop
    10000694:	df858028 	ld	a1,-32728\(gp\)
    10000698:	64a5e95c 	daddiu	a1,a1,-5796
    1000069c:	1000fe90 	b	100000e0 <fn>
    100006a0:	00000000 	nop
    100006a4:	df858020 	ld	a1,-32736\(gp\)
    100006a8:	dca50794 	ld	a1,1940\(a1\)
    100006ac:	1000000c 	b	100006e0 <fn2>
    100006b0:	00000000 	nop
    100006b4:	df858020 	ld	a1,-32736\(gp\)
    100006b8:	dca50728 	ld	a1,1832\(a1\)
    100006bc:	1000fe88 	b	100000e0 <fn>
    100006c0:	00000000 	nop
    100006c4:	df818020 	ld	at,-32736\(gp\)
    100006c8:	0025082d 	daddu	at,at,a1
    100006cc:	dc2507b6 	ld	a1,1974\(at\)
    100006d0:	10000003 	b	100006e0 <fn2>
    100006d4:	00000000 	nop
	\.\.\.
Disassembly of section \.data:

00000000101006e0 <_fdata>:
	\.\.\.

000000001010071c <dg1>:
	\.\.\.

0000000010100758 <sp2>:
	\.\.\.

0000000010100794 <dg2>:
	\.\.\.
Disassembly of section \.got:

00000000101007d0 <_GLOBAL_OFFSET_TABLE_>:
	\.\.\.
    101007d8:	80000000 	.*
    101007dc:	00000000 	.*
    101007e0:	00000000 	.*
    101007e4:	10100000 	.*
    101007e8:	00000000 	.*
    101007ec:	10120000 	.*
    101007f0:	00000000 	.*
    101007f4:	10000000 	.*
    101007f8:	00000000 	.*
	\.\.\.
    1010080c:	100000e0 	.*
    10100810:	00000000 	.*
    10100814:	100006e0 	.*
    10100818:	00000000 	.*
    1010081c:	1010071c 	.*
    10100820:	00000000 	.*
    10100824:	10100794 	.*
