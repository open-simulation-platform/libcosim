pipeline {
    agent none

    options { checkoutToSubdirectory('cse-core') }

    stages {

        stage('Build') {
            parallel {
                stage('Build on Windows') {
                    agent { label 'windows' }
                    
                    environment {
                        CONAN_USER_HOME = "${env.BASE}\\conan-repositories\\${env.EXECUTOR_NUMBER}"
                        CONAN_USER_HOME_SHORT = "${env.CONAN_USER_HOME}"
                        OSP_CONAN_CREDS = credentials('jenkins-osp-conan-creds')
                    }

                    stages {
                        stage('Configure Conan') {
                            steps {
                                sh 'conan remote add osp http://osp-conan.azurewebsites.net/artifactory/api/conan/conan-local --force'
                                sh 'conan user -p $OSP_CONAN_CREDS_PSW -r osp $OSP_CONAN_CREDS_USR'
                            }
                        }
                        stage('Build Debug') {
                            steps {
                                dir('debug-build') {
                                    bat 'conan install ../cse-core -s build_type=Debug -b missing -o ci=True'
                                    bat 'conan build -c -b -i ../cse-core'
                                }
                            }
                        }
                        stage('Build Release') {
                            steps {
                                dir('release-build') {
                                    bat 'conan install ../cse-core -s build_type=Release -b missing -o ci=True'
                                    bat 'conan build -c -b -i ../cse-core'
                                }
                            }
                        }
                        stage ('Test Debug') {
                            steps {
                                dir('debug-build') {
                                    bat 'ctest -C Debug -T Test --no-compress-output --test-output-size-passed 307200 || true'
                                }
                            }
                            post {
                                always{
                                    xunit (
                                        testTimeMargin: '30000',
                                        thresholdMode: 1,
                                        thresholds: [ skipped(failureThreshold: '0'), failed(failureThreshold: '0') ],
                                        tools: [ CTest(pattern: 'debug-build/Testing/**/Test.xml') ]
                                    )
                                }
                                success {
                                    dir('debug-build') {
                                        bat 'conan export-pkg ../cse-core osp/$env.BRANCH_NAME'
                                        bat 'conan upload cse-core/*@osp/$env.BRANCH_NAME --all -r=osp --confirm'
                                    }
                                    archiveArtifacts artifacts: 'install/debug/**/*',  fingerprint: true
                                }
                                cleanup {
                                    dir('debug-build/Testing') {
                                        deleteDir();
                                    }
                                }
                            }
                        }
                        stage ('Test Release') {
                            steps {
                                dir('release-build') {
                                    bat 'ctest -C Release -T Test --no-compress-output --test-output-size-passed 307200 || true'
                                }
                            }
                            post {
                                always{
                                    xunit (
                                        testTimeMargin: '30000',
                                        thresholdMode: 1,
                                        thresholds: [ skipped(failureThreshold: '0'), failed(failureThreshold: '0') ],
                                        tools: [ CTest(pattern: 'release-build/Testing/**/Test.xml') ]
                                    )
                                }
                                success {
                                    dir('release-build') {
                                        bat 'conan export-pkg ../cse-core osp/$env.BRANCH_NAME'
                                        bat 'conan upload cse-core/*@osp/$env.BRANCH_NAME --all -r=osp --confirm'
                                    }
                                    archiveArtifacts artifacts: 'install/release/**/*',  fingerprint: true
                                }
                                cleanup {
                                    dir('release-build/Testing') {
                                        deleteDir();
                                    }
                                }
                            }
                        }
                    }
                }
                stage ( 'Build on Linux with Conan' ) {
                    agent { 
                        dockerfile {
                            filename 'Dockerfile.conan-build'
                            dir 'cse-core/.dockerfiles'
                            label 'linux && docker'
                            args '-v ${HOME}/jenkins_slave/conan-repositories/${EXECUTOR_NUMBER}:/conan_repo'
                        }
                    }

                    environment {
                        CONAN_USER_HOME = '/conan_repo'
                        CONAN_USER_HOME_SHORT = 'None'
                    }
                    
                    stages {
                        stage('Build Debug') {
                            steps {
                                dir('debug-build-conan') {
                                    sh 'conan install ../cse-core -s compiler.libcxx=libstdc++11 -s build_type=Debug -b missing'
                                    sh 'cmake -DCMAKE_BUILD_TYPE=Debug -DCMAKE_INSTALL_PREFIX=../install -DCSECORE_USING_CONAN=TRUE -DCSECORE_BUILD_PRIVATE_APIDOC=ON ../cse-core'
                                    sh 'cmake --build .'
                                    sh 'cmake --build . --target install'
                                }
                            }
                        }
                        stage('Build Release') {
                            steps {
                                dir('release-build-conan') {
                                    sh 'conan install ../cse-core -s compiler.libcxx=libstdc++11 -s build_type=Release -b missing'
                                    sh 'cmake -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=../install -DCSECORE_USING_CONAN=TRUE -DCSECORE_BUILD_PRIVATE_APIDOC=ON ../cse-core'
                                    sh 'cmake --build .'
                                    sh 'cmake --build . --target install'
                                }
                            }
                        }
                        stage ('Test Debug') {
                            steps {
                                dir('debug-build-conan') {
                                    sh '. ./activate_run.sh && ctest -C Debug -T Test --no-compress-output --test-output-size-passed 307200 || true'
                                }
                            }
                        }
                        stage ('Test Release') {
                            steps {
                                dir('release-build-conan') {
                                    sh '. ./activate_run.sh && ctest -C Release -T Test --no-compress-output --test-output-size-passed 307200 || true'
                                }
                            }
                        }
                    }
                    post {
                        always{
                            xunit (
                                testTimeMargin: '30000',
                                thresholdMode: 1,
                                thresholds: [ skipped(failureThreshold: '0'), failed(failureThreshold: '0') ],
                                tools: [ CTest(pattern: '*-build-conan/Testing/**/Test.xml') ]
                            )
                        }
                        success {
                            archiveArtifacts artifacts: 'install/**/*',  fingerprint: true
                        }
                        cleanup {
                            dir('debug-build-conan/Testing') {
                                deleteDir();
                            }
                            dir('release-build-conan/Testing') {
                                deleteDir();
                            }
                        }
                    }
                }
                stage ( 'Build on Linux with Docker' ) {
                    agent { 
                        dockerfile { 
                            filename 'Dockerfile.build'
                            dir 'cse-core/.dockerfiles'
                            label 'linux && docker'
                        }
                    }

                    stages {
                        stage('Build Debug') {
                            steps {
                                dir('debug-build') {
                                    sh 'cmake -DCMAKE_BUILD_TYPE=Debug -DCMAKE_INSTALL_PREFIX=../install -DCSECORE_USING_CONAN=FALSE -DCSECORE_BUILD_PRIVATE_APIDOC=ON ../cse-core'
                                    sh 'cmake --build .'
                                    sh 'cmake --build . --target install'
                                }
                            }
                        }
                        stage('Build Release') {
                            steps {
                                dir('release-build ') {
                                    sh 'cmake -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=../install -DCSECORE_USING_CONAN=FALSE -DCSECORE_BUILD_PRIVATE_APIDOC=ON ../cse-core'
                                    sh 'cmake --build .'
                                    sh 'cmake --build . --target install'
                                }
                            }
                        }
                        stage ('Test Debug') {
                            steps {
                                dir('debug-build') {
                                    sh 'ctest -C Debug -T Test --no-compress-output --test-output-size-passed 307200 || true'
                                }
                            }
                        }
                        stage ('Test Release') {
                            steps {
                                dir('release-build') {
                                    sh 'ctest -C Release -T Test --no-compress-output --test-output-size-passed 307200 || true'
                                }
                            }
                        }
                    }
                    post {
                        always{
                            xunit (
                                testTimeMargin: '30000',
                                thresholdMode: 1,
                                thresholds: [ skipped(failureThreshold: '0'), failed(failureThreshold: '0') ],
                                tools: [ CTest(pattern: '*-build/Testing/**/Test.xml') ]
                            )
                        }
                        success {
                            archiveArtifacts artifacts: 'install/**/*',  fingerprint: true
                        }
                        cleanup {
                            dir('debug-build/Testing') {
                                deleteDir();
                            }
                            dir('release-build/Testing') {
                                deleteDir();
                            }
                        }
                    }
                }
            }
        }
    }
}
