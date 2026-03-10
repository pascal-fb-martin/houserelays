while :
   do echo 0 > /sys/kernel/debug/gpio-mockup/gpiochip2/3
   echo 0 > /sys/kernel/debug/gpio-mockup/gpiochip2/5
   sleep 0.3
   echo 1 > /sys/kernel/debug/gpio-mockup/gpiochip2/3
   echo 1 > /sys/kernel/debug/gpio-mockup/gpiochip2/5
   sleep 0.2
   echo 0 > /sys/kernel/debug/gpio-mockup/gpiochip2/4
   echo 0 > /sys/kernel/debug/gpio-mockup/gpiochip2/6
   sleep 0.4
   echo 1 > /sys/kernel/debug/gpio-mockup/gpiochip2/4
   echo 1 > /sys/kernel/debug/gpio-mockup/gpiochip2/6
   sleep 0.2 
done

